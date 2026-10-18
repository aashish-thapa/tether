#pragma once

#include <string>

namespace tether {

    // Locks the desktop session. With `command` empty this asks logind.
    bool lock_session(const std::string& command);

    // Locked or idle: logind's LockedHint or IdleHint, or the compositor's idle
    // notification. False when none of them can be asked.
    bool session_inactive();

} // namespace tether
