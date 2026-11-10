#include "RunProcess.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

#else

#include <cerrno>
#include <climits>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

namespace
{

constexpr std::size_t OutputHeadBytes   = 8 * 1024;         // 8 KiB of the beginning is kept
constexpr std::size_t OutputHardLimit   = 4 * 1024 * 1024;  // 4 MiB before the middle is dropped
constexpr char        OutputMarker[]    = "\n... output truncated ...\n";
constexpr std::size_t ReadBufferBytes   = 4096;

// Room left for the tail once the head has been reserved, guarded so that no underflow is possible.
constexpr std::size_t TailWindowBytes() noexcept
{
    return MaxProcessOutputBytes > OutputHeadBytes ? MaxProcessOutputBytes - OutputHeadBytes : 0;
}

struct CapturedOutput
{
    std::string text;
    bool        truncated = false;

    void Append(char const* data, std::size_t size);
    [[nodiscard]] std::string Finalize() const;
};

void CapturedOutput::Append(char const* data, std::size_t size)
{
    text.append(data, size);

    if (text.size() <= OutputHardLimit)
        return;

    std::size_t const headBytes     = std::min(OutputHeadBytes, text.size());
    std::size_t const tailWindow    = TailWindowBytes();
    std::size_t const tailAvailable = text.size() - headBytes;
    std::size_t const tailBytes     = std::min(tailWindow, tailAvailable);

    text = text.substr(0, headBytes) + text.substr(text.size() - tailBytes);
    truncated = true;
}

std::string CapturedOutput::Finalize() const
{
    if (!truncated)
        return text;

    std::size_t const headBytes     = std::min(OutputHeadBytes, text.size());
    std::size_t const tailWindow    = TailWindowBytes();
    std::size_t const tailAvailable = text.size() - headBytes;
    std::size_t const tailBytes     = std::min(tailWindow, tailAvailable);

    return text.substr(0, headBytes) + OutputMarker + text.substr(text.size() - tailBytes);
}

} // namespace

#if defined(_WIN32)

namespace
{
std::wstring Utf8ToWide(std::string const& text)
{
    if (text.empty())
        return std::wstring{};

    int const sourceLength = static_cast<int>(text.size());
    int const size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), sourceLength, nullptr, 0);
    if (size <= 0)
        throw std::runtime_error("Failed to convert a command argument to UTF-16");

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    int const converted = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), sourceLength, wide.data(), size);
    if (converted <= 0)
        throw std::runtime_error("Failed to convert a command argument to UTF-16");

    return wide;
}

std::wstring QuoteArgument(std::wstring const& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos)
        return argument;

    std::wstring quoted;
    quoted.push_back(L'"');

    std::size_t backslashes = 0;
    for (wchar_t const character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }

        if (character == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, L'\\');
        quoted.push_back(character);
        backslashes = 0;
    }

    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');

    return quoted;
}

} // namespace

ProcessResult RunProcess(std::filesystem::path const& executable,
                         std::vector<std::string> const& arguments,
                         std::filesystem::path const& workingDirectory,
                         std::chrono::seconds timeout)
{
    ProcessResult result;
    std::wstring commandLine = QuoteArgument(Utf8ToWide(executable.string()));

    for (auto const& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine.append(QuoteArgument(Utf8ToWide(argument)));
    }

    std::wstring const workingDirectoryText =
        workingDirectory.empty() ? std::wstring{} : Utf8ToWide(workingDirectory.string());
    std::vector<wchar_t> workingDirectoryBuffer(workingDirectoryText.begin(), workingDirectoryText.end());
    workingDirectoryBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES pipeAttributes{};
    pipeAttributes.nLength              = static_cast<DWORD>(sizeof(pipeAttributes));
    pipeAttributes.lpSecurityDescriptor = nullptr;
    pipeAttributes.bInheritHandle       = TRUE;

    HANDLE readHandle  = nullptr;
    HANDLE writeHandle = nullptr;
    if (::CreatePipe(&readHandle, &writeHandle, &pipeAttributes, 0) == 0)
        throw std::runtime_error("Failed to create a pipe for the build command");

    if (::SetHandleInformation(writeHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) == 0)
    {
        ::CloseHandle(readHandle);
        ::CloseHandle(writeHandle);
        throw std::runtime_error("Failed to configure the build command pipe");
    }

    if (::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0) == 0)
    {
        ::CloseHandle(readHandle);
        ::CloseHandle(writeHandle);
        throw std::runtime_error("Failed to configure the build command pipe");
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb         = static_cast<DWORD>(sizeof(startupInfo));
    startupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startupInfo.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writeHandle;
    startupInfo.hStdError  = writeHandle;

    PROCESS_INFORMATION processInfo{};

    BOOL const created = ::CreateProcessW(nullptr,
                                          &commandLine[0],
                                          nullptr,
                                          nullptr,
                                          TRUE,
                                          CREATE_NO_WINDOW,
                                          nullptr,
                                          workingDirectoryText.empty() ? nullptr : workingDirectoryBuffer.data(),
                                          &startupInfo,
                                          &processInfo);

    // The child owns the write end from here on; the parent must never keep it open, otherwise
    // the read end would never report EOF. This is its single CloseHandle on every path.
    ::CloseHandle(writeHandle);

    if (created == 0)
    {
        ::CloseHandle(readHandle);
        throw std::runtime_error("Failed to start the build command");
    }

    CapturedOutput captured;

    std::thread reader([&captured, readHandle]()
    {
        std::array<char, ReadBufferBytes> buffer;
        for (;;)
        {
            DWORD bytesRead = 0;
            BOOL const readOk =
                ::ReadFile(readHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
            if (readOk == 0 || bytesRead == 0)
                break;

            captured.Append(buffer.data(), static_cast<std::size_t>(bytesRead));
        }
    });

    constexpr std::int64_t MaxWaitMilliseconds = 0x7FFFFFFF;  // safely inside the DWORD range

    auto const timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    auto const clampedMs = std::clamp<std::int64_t>(static_cast<std::int64_t>(timeoutMs), 1, MaxWaitMilliseconds);
    DWORD const waitMilliseconds = static_cast<DWORD>(clampedMs);

    bool timedOut = false;
    if (::WaitForSingleObject(processInfo.hProcess, waitMilliseconds) == WAIT_TIMEOUT)
    {
        ::TerminateProcess(processInfo.hProcess, 1);
        timedOut = true;
    }

    ::WaitForSingleObject(processInfo.hProcess, INFINITE);
    reader.join();

    DWORD exitCodeValue = 0;
    bool const exitCodeAvailable = ::GetExitCodeProcess(processInfo.hProcess, &exitCodeValue) != 0;

    ::CloseHandle(readHandle);
    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);

    result.exitCode  = (timedOut || !exitCodeAvailable) ? -1 : static_cast<int>(exitCodeValue);
    result.timedOut  = timedOut;
    result.truncated = captured.truncated;
    result.output    = captured.Finalize();

    return result;
}

#else

ProcessResult RunProcess(std::filesystem::path const& executable,
                         std::vector<std::string> const& arguments,
                         std::filesystem::path const& workingDirectory,
                         std::chrono::seconds timeout)
{
    ProcessResult result;
    CapturedOutput captured;

    // Everything the child needs must be prepared before forking: after fork only
    // async-signal-safe calls are allowed.
    std::vector<std::string> argvStorage;
    argvStorage.reserve(arguments.size() + 1);
    argvStorage.push_back(executable.string());
    for (auto const& argument : arguments)
        argvStorage.push_back(argument);

    std::vector<char*> argv;
    argv.reserve(argvStorage.size() + 1);
    for (std::size_t i = 0; i < argvStorage.size(); ++i)
        argv.push_back(argvStorage[i].data());
    argv.push_back(nullptr);

    std::string const workingDirectoryText =
        workingDirectory.empty() ? std::string{} : workingDirectory.string();

    int outputPipe[2];
    if (::pipe(outputPipe) != 0)
        throw std::runtime_error("Failed to create a pipe for the build command");

    pid_t const pid = ::fork();
    if (pid < 0)
    {
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        throw std::runtime_error("Failed to start the build command");
    }

    if (pid == 0)
    {
        ::close(outputPipe[0]);

        if (::dup2(outputPipe[1], STDOUT_FILENO) < 0 || ::dup2(outputPipe[1], STDERR_FILENO) < 0)
            ::_exit(127);

        if (outputPipe[1] > STDERR_FILENO)
            ::close(outputPipe[1]);

        if (!workingDirectoryText.empty() && ::chdir(workingDirectoryText.c_str()) != 0)
            ::_exit(127);

        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(outputPipe[1]);

    bool timedOut = false;
    auto const deadline = std::chrono::steady_clock::now() + timeout;

    for (;;)
    {
        auto const remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero())
        {
            timedOut = true;
            break;
        }

        auto const remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        std::int64_t const requestedTimeoutMs = std::max<std::int64_t>(static_cast<std::int64_t>(remainingMs), 1);
        int const pollTimeoutMs = static_cast<int>(std::min<std::int64_t>(requestedTimeoutMs, INT_MAX));

        pollfd descriptor;
        descriptor.fd      = outputPipe[0];
        descriptor.events  = POLLIN;
        descriptor.revents = 0;

        int const ready = ::poll(&descriptor, 1, pollTimeoutMs);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;

            break;
        }

        if (ready == 0)
        {
            timedOut = true;
            break;
        }

        std::array<char, ReadBufferBytes> buffer;
        ssize_t const bytesRead = ::read(outputPipe[0], buffer.data(), buffer.size());
        if (bytesRead > 0)
        {
            captured.Append(buffer.data(), static_cast<std::size_t>(bytesRead));
            continue;
        }

        if (bytesRead == 0)
            break;

        if (errno != EINTR && errno != EAGAIN)
            break;
    }

    ::close(outputPipe[0]);

    if (timedOut)
        ::kill(pid, SIGKILL);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }

    result.exitCode  = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.timedOut  = timedOut;
    result.truncated = captured.truncated;
    result.output    = captured.Finalize();

    return result;
}

#endif
