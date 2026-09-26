// The settings dialog.

#pragma once

#include <windows.h>
#include "Settings.h"

namespace SettingsDlg
{

// Modal. On OK, writes the edited values into `settings`, saves the INI and
// applies the start-on-logon registry change. Returns true when something was
// saved.
bool Show(HWND owner, HINSTANCE inst, Settings &settings);

} // namespace SettingsDlg
