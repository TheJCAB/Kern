#pragma once

#include <memory>

// Asks the operating system not to suspend the machine on its own for as long as this object
// exists, so that power management cannot interrupt a long agent session.
//
// Windows: SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) on the calling thread,
// undone by the destructor. The display is deliberately left free to switch off.
// Linux: a `systemd-inhibit --what=idle` helper process holds the inhibitor lock, and macOS gets
// the same effect from `caffeinate -i -s`; the destructor stops the helper. Only automatic idle
// suspend is blocked, so a manual suspend (lid close, sleep button) still works. The helper's
// command ends when this process closes a pipe, so a crashed kern releases the lock too.
// Anywhere else, or when no inhibitor can be obtained, the object does nothing at all.
class SleepInhibitor
{
public:
    SleepInhibitor();
    ~SleepInhibitor();

    SleepInhibitor(SleepInhibitor const&)            = delete;
    SleepInhibitor& operator=(SleepInhibitor const&) = delete;
    SleepInhibitor(SleepInhibitor&&)                 = delete;
    SleepInhibitor& operator=(SleepInhibitor&&)      = delete;

    // True when the operating system actually accepted the request.
    [[nodiscard]] bool IsActive() const;

private:
    struct Pimpl;
    std::unique_ptr<Pimpl> m_pimpl;
};
