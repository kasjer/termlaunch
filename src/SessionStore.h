// Creating PuTTY / KiTTY sessions, in whichever place the terminal reads them
// from.
//
// Two backends:
//
//   Directory  one file per session under a Sessions folder. ASCII, LF line
//              endings, one `Key\value\` pair per line, no header. The file
//              name is the session name run through PuTTY's mungestr()
//              (see Util::MungeSessionName).
//
//   Registry   one subkey per session under the Sessions key, by default
//              HKCU\SOFTWARE\9bis.com\KiTTY\Sessions. The subkey name is
//              munged the same way; each setting is REG_SZ or REG_DWORD.
//
// Which one is in force comes from kitty.ini (see KittyConfig), unless the
// user pins it in Settings.

#pragma once

#include <string>
#include "KittyConfig.h"
#include "Settings.h"

namespace SessionStore
{

// "COM4" + 1000000 -> "COM4_1000000"
std::wstring NameFor(const std::wstring &portName, int speed);

// Full path of the file backing a session name (directory mode only).
std::wstring PathFor(const std::wstring &sessionsDir, const std::wstring &sessionName);

struct Resolved
{
    SaveMode     mode = SaveMode::Directory;
    std::wstring sessionsDir;      // directory mode
    std::wstring registryPath;     // registry mode, relative to HKEY_CURRENT_USER
    std::wstring fileExtension;    // directory mode, when kitty.ini sets one
    bool         fromIni = false;  // mode came from kitty.ini, not the override
    std::wstring iniPath;          // which kitty.ini decided it, when any
};

// Works out where a launch would actually write, honouring the Settings
// override first and the detected kitty.ini second.
Resolved Resolve(const Settings &settings);

// Creates the session if it is missing. Existing sessions are left untouched.
//
// A new session starts from `Default Settings` where that exists — the file in
// directory mode, the subkey in registry mode — so the user's colours and
// fonts carry over; otherwise from a small built-in template. Either way the
// serial keys are set to match the port and speed.
//
// `created` reports whether anything was actually written.
bool EnsureExists(const Settings &settings, const std::wstring &portName, int speed,
                  bool &created, std::wstring &error);

// One line naming the live location, for the About box and the settings dialog.
std::wstring DescribeLocation(const Settings &settings);

// Opens Explorer on the sessions folder, or Registry Editor on the key.
void OpenLocation(const Settings &settings);

// Menu label for the above, which differs per backend.
std::wstring OpenLocationLabel(const Settings &settings);

} // namespace SessionStore
