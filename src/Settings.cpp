#include "Settings.h"
#include "Util.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{

const wchar_t *const kSection      = L"TermLaunch";
const wchar_t *const kIniFileName  = L"termlaunch.ini";
const wchar_t *const kRunKeyPath   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t *const kRunValueName = L"TermLaunch";

std::wstring AppDataIniPath()
{
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        dir = Util::PathJoin(roaming, L"TermLaunch");
        CoTaskMemFree(roaming);
    }
    if (dir.empty()) return std::wstring();
    Util::EnsureDirectory(dir);
    return Util::PathJoin(dir, kIniFileName);
}

std::wstring ResolveIniPath()
{
    const std::wstring exeDir = Util::GetExeDir();
    const std::wstring beside = Util::PathJoin(exeDir, kIniFileName);

    // An INI already sitting next to the exe wins — that is the portable case.
    if (Util::FileExists(beside)) return beside;
    if (Util::IsDirectoryWritable(exeDir)) return beside;

    std::wstring fallback = AppDataIniPath();
    return fallback.empty() ? beside : fallback;
}

// WritePrivateProfileStringW only preserves non-ASCII text if the file is
// already UTF-16LE, which it detects from the BOM. Create it that way.
void EnsureUnicodeIni(const std::wstring &path)
{
    if (Util::FileExists(path)) return;
    Util::Handle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.Valid()) return;
    const WCHAR bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(h.Get(), &bom, sizeof(bom), &written, nullptr);
}

std::wstring ReadIniString(const std::wstring &ini, const wchar_t *key,
                           const std::wstring &fallback)
{
    wchar_t buf[2048];
    DWORD n = GetPrivateProfileStringW(kSection, key, fallback.c_str(),
                                       buf, ARRAYSIZE(buf), ini.c_str());
    return std::wstring(buf, n);
}

int ReadIniInt(const std::wstring &ini, const wchar_t *key, int fallback)
{
    return (int)GetPrivateProfileIntW(kSection, key, fallback, ini.c_str());
}

bool WriteIniString(const std::wstring &ini, const wchar_t *key,
                    const std::wstring &value)
{
    return WritePrivateProfileStringW(kSection, key, value.c_str(), ini.c_str()) != FALSE;
}

bool WriteIniInt(const std::wstring &ini, const wchar_t *key, int value)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    return WriteIniString(ini, key, buf);
}

int ClampSpeed(int speed)
{
    if (speed < 50) return 0;
    if (speed > 20000000) return 0;
    return speed;
}

} // namespace

// ---- Settings ------------------------------------------------------------

void Settings::Load()
{
    iniPath = ResolveIniPath();

    Settings defaults;   // the field initialisers above are the fallbacks

    terminalPath = Util::Trim(ReadIniString(iniPath, L"TerminalPath", defaults.terminalPath));
    sessionsDir  = Util::Trim(ReadIniString(iniPath, L"SessionsDir",  defaults.sessionsDir));

    // An empty sessions folder means "next to the terminal", which is where
    // KiTTY portable keeps its own Sessions directory.
    if (sessionsDir.empty() && !terminalPath.empty())
    {
        sessionsDir = Util::PathJoin(Util::GetDirectoryOf(terminalPath), L"Sessions");
    }

    SetSpeedsFromText(ReadIniString(iniPath, L"Speeds", defaults.SpeedsAsText()));

    defaultSpeed = ClampSpeed(ReadIniInt(iniPath, L"DefaultSpeed", defaults.defaultSpeed));
    if (defaultSpeed == 0) defaultSpeed = defaults.defaultSpeed;

    dataBits     = ReadIniInt(iniPath, L"DataBits",     defaults.dataBits);
    stopHalfbits = ReadIniInt(iniPath, L"StopHalfbits", defaults.stopHalfbits);
    parity       = ReadIniInt(iniPath, L"Parity",       defaults.parity);
    flowControl  = ReadIniInt(iniPath, L"FlowControl",  defaults.flowControl);

    detectBusy       = ReadIniInt(iniPath, L"DetectBusy",       defaults.detectBusy ? 1 : 0) != 0;
    animateOnArrival = ReadIniInt(iniPath, L"AnimateOnArrival", defaults.animateOnArrival ? 1 : 0) != 0;
    showFriendlyName = ReadIniInt(iniPath, L"ShowFriendlyNames", defaults.showFriendlyName ? 1 : 0) != 0;

    if (dataBits < 5 || dataBits > 8) dataBits = 8;
    if (stopHalfbits != 2 && stopHalfbits != 3 && stopHalfbits != 4) stopHalfbits = 2;
    if (parity < 0 || parity > 4) parity = 0;
    if (flowControl < 0 || flowControl > 3) flowControl = 0;

    // Keep the default reachable from the menu.
    if (std::find(speeds.begin(), speeds.end(), defaultSpeed) == speeds.end())
    {
        speeds.push_back(defaultSpeed);
        std::sort(speeds.begin(), speeds.end());
    }
}

bool Settings::Save(std::wstring &error) const
{
    if (iniPath.empty())
    {
        error = L"No configuration file path is available.";
        return false;
    }

    EnsureUnicodeIni(iniPath);

    const bool ok =
        WriteIniString(iniPath, L"TerminalPath", terminalPath) &&
        WriteIniString(iniPath, L"SessionsDir",  sessionsDir)  &&
        WriteIniString(iniPath, L"Speeds",       SpeedsAsText()) &&
        WriteIniInt(iniPath, L"DefaultSpeed", defaultSpeed) &&
        WriteIniInt(iniPath, L"DataBits",     dataBits)     &&
        WriteIniInt(iniPath, L"StopHalfbits", stopHalfbits) &&
        WriteIniInt(iniPath, L"Parity",       parity)       &&
        WriteIniInt(iniPath, L"FlowControl",  flowControl)  &&
        WriteIniInt(iniPath, L"DetectBusy",        detectBusy ? 1 : 0)       &&
        WriteIniInt(iniPath, L"AnimateOnArrival",  animateOnArrival ? 1 : 0) &&
        WriteIniInt(iniPath, L"ShowFriendlyNames", showFriendlyName ? 1 : 0);

    if (!ok)
    {
        error = L"Could not write " + iniPath + L"\n\n" +
            Util::FormatLastError(GetLastError());
        return false;
    }
    return true;
}

std::wstring Settings::SpeedsAsText() const
{
    std::wstring out;
    for (size_t i = 0; i < speeds.size(); ++i)
    {
        if (i) out += L",";
        wchar_t buf[32];
        swprintf_s(buf, L"%d", speeds[i]);
        out += buf;
    }
    return out;
}

void Settings::SetSpeedsFromText(const std::wstring &text)
{
    std::vector<int> parsed;
    for (const std::wstring &raw : Util::Split(text, L','))
    {
        std::wstring token = Util::Trim(raw);
        if (token.empty()) continue;
        int value = _wtoi(token.c_str());
        value = ClampSpeed(value);
        if (value == 0) continue;
        if (std::find(parsed.begin(), parsed.end(), value) == parsed.end())
            parsed.push_back(value);
    }
    if (parsed.empty()) return;   // keep whatever we had rather than emptying the menu
    std::sort(parsed.begin(), parsed.end());
    speeds = parsed;
}

bool Settings::Validate(std::wstring &problem) const
{
    if (terminalPath.empty())
    {
        problem = L"No terminal program is configured.";
        return false;
    }
    if (!Util::FileExists(terminalPath))
    {
        problem = L"The terminal program was not found:\n" + terminalPath;
        return false;
    }
    if (sessionsDir.empty())
    {
        problem = L"No sessions folder is configured.";
        return false;
    }
    return true;
}

// ---- AutoStart -----------------------------------------------------------

namespace AutoStart
{

namespace
{

std::wstring QuotedExePath()
{
    return L"\"" + Util::GetExePath() + L"\"";
}

std::wstring ReadRunValue()
{
    Util::RegKey key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ,
                      key.Receive()) != ERROR_SUCCESS)
        return std::wstring();

    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(key.Get(), kRunValueName, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
        return std::wstring();
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::wstring();

    std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key.Get(), kRunValueName, nullptr, &type,
                         reinterpret_cast<BYTE *>(&buf[0]), &cb) != ERROR_SUCCESS)
        return std::wstring();

    buf.resize(wcslen(buf.c_str()));
    return buf;
}

} // namespace

bool IsEnabled()
{
    return !ReadRunValue().empty();
}

bool SetEnabled(bool enable, std::wstring &error)
{
    Util::RegKey key;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE,
                             nullptr, key.Receive(), nullptr);
    if (r != ERROR_SUCCESS)
    {
        error = L"Could not open the Run key:\n" + Util::FormatLastError((DWORD)r);
        return false;
    }

    if (enable)
    {
        const std::wstring value = QuotedExePath();
        r = RegSetValueExW(key.Get(), kRunValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(value.c_str()),
                           (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        if (r != ERROR_SUCCESS)
        {
            error = L"Could not enable start on logon:\n" + Util::FormatLastError((DWORD)r);
            return false;
        }
    }
    else
    {
        r = RegDeleteValueW(key.Get(), kRunValueName);
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND)
        {
            error = L"Could not disable start on logon:\n" + Util::FormatLastError((DWORD)r);
            return false;
        }
    }
    return true;
}

void RefreshPathIfEnabled()
{
    const std::wstring current = ReadRunValue();
    if (current.empty()) return;              // not enabled; leave it alone

    const std::wstring wanted = QuotedExePath();
    if (Util::IEquals(current, wanted)) return;

    std::wstring ignored;
    SetEnabled(true, ignored);                // the exe moved — point at the new one
}

} // namespace AutoStart
