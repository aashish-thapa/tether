#pragma once

#include <string>

namespace tether {

    // Locks the desktop session. With `command` empty this asks logind.
    bool lock_session(const std::string& command);

    // logind's LockedHint for this session. False when logind cannot be asked.
    bool session_locked();

} // namespace tether
