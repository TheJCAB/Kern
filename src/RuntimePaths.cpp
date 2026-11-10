#include "RuntimePaths.h"

#include "Credentials.h"
#include "FileUtilities.h"

#include <string_view>
#include <system_error>
#include <utility>

namespace
{

// Name of the per-user directory that holds everything the program writes for one user.
constexpr char ApplicationDirectoryName[] = "Kern";

// Name of the data directory beside the executable, where a build tree keeps its runtime data.
constexpr char DataDirectoryName[] = "data";

// Name of the directory that receives log files and session transcripts.
constexpr char LogsDirectoryName[] = "logs";

// Where the installed runtime data lives, expressed relative to the directory that holds the executable. The build computes it from the install destinations rather than from a prefix, so it carries no machine-specific path and an installed tree can be moved or copied freely. Empty when the build could not derive a relative layout.
#ifdef KERN_INSTALL_DATA_DIR_RELATIVE
constexpr std::string_view InstalledDataDirectoryRelative = KERN_INSTALL_DATA_DIR_RELATIVE;
#else
constexpr std::string_view InstalledDataDirectoryRelative = "";
#endif

// What was found beside the executable, resolved once per process.
struct RuntimeLayout
{
    std::filesystem::path dataDirectory;
    bool                  installed = false;
};

// Decides once per process whether this is an installed tree or a build tree: the installed data directory wins when it exists, and the one beside the executable is used otherwise.
RuntimeLayout const& GetRuntimeLayout()
{
    static RuntimeLayout const layout = []
    {
        std::filesystem::path const executableDirectory = GetExecutableDirectory();
        if (!InstalledDataDirectoryRelative.empty())
        {
            auto installed = executableDirectory / std::filesystem::path{ InstalledDataDirectoryRelative };
            std::error_code error;
            if (std::filesystem::is_directory(installed, error) && !error)
            {
                return RuntimeLayout{ std::move(installed), true };
            }
        }
        return RuntimeLayout{ executableDirectory / DataDirectoryName, false };
    }
    ();
    return layout;
}

// Per-user directory that an ordinary user can write to, derived the same way as the services file in GetServicesConfigPath(); empty when no environment variable names one.
std::filesystem::path GetUserStateDirectory()
{
#if _WIN32
    if (auto const localAppData = ReadEnvironmentVariable("LOCALAPPDATA"); localAppData && !localAppData->empty())
    {
        return std::filesystem::path{ *localAppData } / ApplicationDirectoryName;
    }
#else
    if (auto const xdgStateHome = ReadEnvironmentVariable("XDG_STATE_HOME"); xdgStateHome && !xdgStateHome->empty())
    {
        return std::filesystem::path{ *xdgStateHome } / ApplicationDirectoryName;
    }
    if (auto const home = ReadEnvironmentVariable("HOME"); home && !home->empty())
    {
        return std::filesystem::path{ *home } / ".local" / "state" / ApplicationDirectoryName;
    }
#endif
    return {};
}

}

std::filesystem::path GetRuntimeDataDirectory()
{
    return GetRuntimeLayout().dataDirectory;
}

std::filesystem::path GetDefaultLogDirectory()
{
    RuntimeLayout const& layout = GetRuntimeLayout();
    if (!layout.installed)
    {
        return GetExecutableDirectory() / LogsDirectoryName;
    }
    if (auto const userStateDirectory = GetUserStateDirectory(); !userStateDirectory.empty())
    {
        return userStateDirectory / LogsDirectoryName;
    }
    return GetExecutableDirectory() / LogsDirectoryName;
}
