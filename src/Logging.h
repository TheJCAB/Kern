#pragma once

#include <format>
#include <optional>
#include <string_view>
#include <utility>

// Diagnostic logging, written to stderr so it doesn't mix with the agent's console output.
// Secrets (API keys) must never be passed to these functions; HTTP headers must not be logged.
enum class LogLevel
{
    off,
    error,
    warning,
    info,
    debug, // requests, responses (status, sizes, timing), finish reasons
    trace, // every raw server-sent event
};

// Accepts "off", "error", "warning", "info", "debug" and "trace" (lowercase).
std::optional<LogLevel> ParseLogLevel(std::string_view name);

void     SetLogLevel(LogLevel level);
LogLevel GetLogLevel();

inline bool IsLogEnabled(LogLevel const level) { return level != LogLevel::off && level <= GetLogLevel(); }

void LogMessage(LogLevel level, std::string_view message);

template <typename... Args>
void Log(LogLevel const level, std::format_string<Args...> const format, Args&&... args)
{
    if (IsLogEnabled(level))
    {
        LogMessage(level, std::format(format, std::forward<Args>(args)...));
    }
}
