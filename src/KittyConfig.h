// Reading kitty.ini to find out where KiTTY keeps its sessions.
//
// KiTTY stores sessions either as files in a Sessions directory or in the
// Windows registry, and decides by the `savemode` line in kitty.ini. When no
// kitty.ini is found, or it states no savemode, KiTTY defaults to the registry.

#pragma once

#include <string>

enum class SaveMode
{
    Directory,    // savemode=dir   — one file per session in a Sessions folder
    Registry,     // savemode=registry — HKCU\SOFTWARE\9bis.com\KiTTY\Sessions
    SingleFile,   // savemode=file  — KiTTY calls this unmaintained; unsupported here
};

namespace KittyConfig
{

struct Config
{
    bool         found = false;                   // a kitty.ini was located
    std::wstring iniPath;                         // which one, for display
    bool         saveModeStated = false;          // the ini carried a savemode= line
    SaveMode     saveMode = SaveMode::Registry;   // KiTTY's default when unstated
    std::wstring configDir;                       // [KiTTY] configdir, when set
    std::wstring fileExtension;                   // [KiTTY] fileextension, e.g. ".ktx"
};

// Looks beside the terminal executable first — that is portable mode, and it
// wins — then in %APPDATA%\KiTTY. `found` stays false when neither exists,
// which still means Registry, because that is KiTTY's default.
Config Detect(const std::wstring &terminalPath);

// Where directory-mode sessions live according to this config: configdir when
// the ini sets one, otherwise the terminal's own folder, plus "Sessions".
// Empty when `terminalPath` gives us nothing to work from.
std::wstring SessionsDirFor(const Config &config, const std::wstring &terminalPath);

const wchar_t *SaveModeName(SaveMode mode);

} // namespace KittyConfig
