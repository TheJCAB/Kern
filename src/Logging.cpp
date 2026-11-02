#include "Logging.h"

#include <cstdio>

namespace
{

LogLevel g_level = LogLevel::warning;

char const* LevelName(LogLevel const level)
{
    switch (level)
    {
    case LogLevel::error:   return "error";
    case LogLevel::warning: return "warning";
    case LogLevel::info:    return "info";
    case LogLevel::debug:   return "debug";
    case LogLevel::trace:   return "trace";
    default:                return "?";
    }
}

} // namespace

std::optional<LogLevel> ParseLogLevel(std::string_view const name)
{
    if (name == "off"    ) return LogLevel::off;
    if (name == "error"  ) return LogLevel::error;
    if (name == "warning") return LogLevel::warning;
    if (name == "info"   ) return LogLevel::info;
    if (name == "debug"  ) return LogLevel::debug;
    if (name == "trace"  ) return LogLevel::trace;
    return std::nullopt;
}

void SetLogLevel(LogLevel const level) { g_level = level; }

LogLevel GetLogLevel() { return g_level; }

void LogMessage(LogLevel const level, std::string_view const message)
{
    if (!IsLogEnabled(level))
    {
        return;
    }
    std::fprintf(stderr, "[%s] %.*s\n", LevelName(level), static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
}
