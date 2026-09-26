// Reading and writing PuTTY / KiTTY portable session files.
//
// Format: ASCII, LF line endings, one `Key\value\` pair per line, no header.
// The file name is the session name run through PuTTY's mungestr()
// (see Util::MungeSessionName).

#pragma once

#include <string>
#include "Settings.h"

namespace SessionFile
{

// "COM4" + 1000000 -> "COM4_1000000"
std::wstring NameFor(const std::wstring &portName, int speed);

// Full path of the file backing a session name, inside `sessionsDir`.
std::wstring PathFor(const std::wstring &sessionsDir, const std::wstring &sessionName);

// Creates the session file if it is missing, with SerialLine and SerialSpeed
// matching the session name. Existing files are left untouched.
//
// New files start from `Default%20Settings` in the sessions folder when that
// exists, so the user's colours and fonts carry over; otherwise from a small
// built-in template.
//
// `created` reports whether a file was actually written.
bool EnsureExists(const Settings &settings,
                  const std::wstring &portName,
                  int speed,
                  bool &created,
                  std::wstring &error);

} // namespace SessionFile
