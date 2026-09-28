// Starts the configured terminal against a session.

#pragma once

#include <string>
#include "Settings.h"

namespace Launcher
{

// Ensures the session file exists, then runs
//     "<terminalPath>" @<Port>_<Speed>
// with the terminal's own directory as the working directory (KiTTY portable
// resolves its Sessions folder relative to that).
bool Launch(const Settings &settings,
            const std::wstring &portName,
            int speed,
            std::wstring &error);

} // namespace Launcher
