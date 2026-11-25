#include "SleepInhibitor.h"

#include "Logging.h"

#include <memory>

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#else

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#endif

#if !defined(_WIN32)

namespace
{

struct HelperProcess
{
    pid_t pid = -1;    // the process id of the inhibit helper, -1 when none is running
    int   holdFd = -1; // the write end of the pipe whose read end is the helper command's stdin
};

constexpr int PollIntervalMs    = 10;
constexpr int StartPollAttempts = 10;  // how many times to check whether the helper died at once
constexpr int StopPollAttempts  = 50;  // how long to wait for a graceful stop before escalating

// Starts the helper process that holds the idle inhibitor lock. Returns an inactive HelperProcess
// when no inhibitor could be obtained.
HelperProcess StartIdleInhibitor()
{
    int holdPipe[2];
    if (::pipe(holdPipe) != 0)
        return HelperProcess{};

    // Both ends are marked close-on-exec, and the design below depends on that: the write end is
    // closed by the exec, so it must never be inherited by the helper nor by any process later
    // started by RunProcess. An unmarked descriptor would survive the exec and keep the helper's
    // command running past the end of the session, so a mark that fails (which practically never
    // happens) gives up on the inhibitor altogether.
    if (::fcntl(holdPipe[0], F_SETFD, FD_CLOEXEC) < 0 || ::fcntl(holdPipe[1], F_SETFD, FD_CLOEXEC) < 0)
    {
        ::close(holdPipe[0]);
        ::close(holdPipe[1]);
        return HelperProcess{};
    }

    // Everything the child needs must be prepared before forking: after fork only
    // async-signal-safe calls are allowed.
    //
    // The command the helper runs is `cat` reading a pipe on purpose: closing the write end makes
    // that command exit, which releases the lock and ends the helper itself.
#if defined(__APPLE__)
    std::vector<std::string> const arguments = { "caffeinate", "-i", "-s", "cat" };
#else
    std::vector<std::string> const arguments = { "systemd-inhibit",
                                                 "--what=idle",
                                                 "--who=kern",
                                                 "--why=Kern agent session in progress",
                                                 "--mode=block",
                                                 "cat" };
#endif

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string const& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    pid_t const pid = ::fork();
    if (pid < 0)
    {
        ::close(holdPipe[0]);
        ::close(holdPipe[1]);
        return HelperProcess{};
    }

    if (pid == 0)
    {
        ::close(holdPipe[1]);

        if (::dup2(holdPipe[0], STDIN_FILENO) < 0)
            ::_exit(127);
        if (holdPipe[0] > STDERR_FILENO)
            ::close(holdPipe[0]);

        // A helper that cannot inhibit anything complains on its stderr, and none of that noise may
        // reach the console.
        int const nullDevice = ::open("/dev/null", O_RDWR);
        if (nullDevice >= 0)
        {
            ::dup2(nullDevice, STDOUT_FILENO);
            ::dup2(nullDevice, STDERR_FILENO);
            if (nullDevice > STDERR_FILENO)
                ::close(nullDevice);
        }

        ::execvp(argv[0], argv.data());

        // Reaching the code after execvp means the exec failed.
        ::_exit(127);
    }

    ::close(holdPipe[0]);

    // The helper is given a moment to fail so that IsActive() means something: a helper that exits
    // at once (the tool is missing, or there is no session bus to talk to, as in a container or a
    // plain ssh session) must not be reported as a working inhibitor.
    int waitStatus = 0;
    for (int attempt = 0; attempt < StartPollAttempts; ++attempt)
    {
        pid_t const result = ::waitpid(pid, &waitStatus, WNOHANG);
        if (result == pid)
        {
            ::close(holdPipe[1]);
            Log(LogLevel::debug,
                "keep-awake: the inhibit helper exited at once (exit status {})",
                WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1);
            return HelperProcess{};
        }
        if (result < 0 && errno != EINTR)
        {
            ::close(holdPipe[1]);
            return HelperProcess{};
        }

        if (attempt + 1 < StartPollAttempts)
            std::this_thread::sleep_for(std::chrono::milliseconds(PollIntervalMs));
    }

    return HelperProcess{ .pid = pid, .holdFd = holdPipe[1] };
}

} // namespace

// Deliberately not handled: if kern itself is killed by a signal the helper is left behind, and the
// lock is released anyway because the helper's command sees end-of-file on the pipe.

#endif

struct SleepInhibitor::Pimpl
{
#if defined(_WIN32)
    bool active = false; // true while the thread execution state is set
#else
    pid_t child  = -1;
    int   holdFd = -1;
    bool  active = false;
#endif
};

SleepInhibitor::SleepInhibitor() : m_pimpl(std::make_unique<Pimpl>())
{
#if defined(_WIN32)
    // ES_SYSTEM_REQUIRED keeps the system running, ES_CONTINUOUS makes the request sticky until the
    // next call. ES_DISPLAY_REQUIRED is deliberately NOT set, so the monitor can still switch
    // itself off. The call returns 0 when it fails.
    m_pimpl->active = (::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0);
    if (!m_pimpl->active)
        Log(LogLevel::warning, "keep-awake: the system refused the request not to suspend");
#else
    HelperProcess const helper = StartIdleInhibitor();
    m_pimpl->child              = helper.pid;
    m_pimpl->holdFd             = helper.holdFd;
    m_pimpl->active             = helper.pid > 0;
    if (!m_pimpl->active)
        Log(LogLevel::debug, "keep-awake: no idle inhibitor available, power management is unchanged");
#endif
}

SleepInhibitor::~SleepInhibitor()
{
#if defined(_WIN32)
    if (m_pimpl->active)
    {
        // This returns power management to the system's own policy.
        ::SetThreadExecutionState(ES_CONTINUOUS);
        m_pimpl->active = false;
    }
#else
    // Closing the write end gives the helper's command end-of-file, so it releases the lock and
    // exits by itself.
    if (m_pimpl->holdFd >= 0)
    {
        ::close(m_pimpl->holdFd);
        m_pimpl->holdFd = -1;
    }

    if (m_pimpl->child > 0)
    {
        ::kill(m_pimpl->child, SIGTERM);

        int waitStatus = 0;
        for (int attempt = 0; attempt < StopPollAttempts; ++attempt)
        {
            if (::waitpid(m_pimpl->child, &waitStatus, WNOHANG) != 0)
            {
                m_pimpl->child = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(PollIntervalMs));
        }

        // SIGKILL cannot be ignored, so the final reap cannot block for longer than the kernel
        // needs to finish the job.
        if (m_pimpl->child > 0)
        {
            ::kill(m_pimpl->child, SIGKILL);
            while (::waitpid(m_pimpl->child, &waitStatus, 0) < 0 && errno == EINTR)
            {
            }
            m_pimpl->child = -1;
        }
    }

    m_pimpl->active = false;
#endif
}

bool SleepInhibitor::IsActive() const
{
    return m_pimpl->active;
}
