#include "SessionStore.h"
#include "Util.h"

#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <utility>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{

// A single session setting, carrying the type the registry backend needs.
// Directory mode renders a number as its decimal text.
struct SessionValue
{
    const wchar_t *name;
    bool           isDword;
    const wchar_t *text;     // when !isDword
    DWORD          number;   // when isDword
};

// Used when there is no "Default Settings" to copy from. PuTTY and KiTTY
// supply their own defaults for anything absent, so this stays short.
const SessionValue kFallbackTemplate[] = {
    { L"Present",            true,  nullptr,        1 },
    { L"Protocol",           false, L"serial",      0 },
    { L"HostName",           false, L"",            0 },
    { L"PortNumber",         true,  nullptr,        0 },
    { L"CloseOnExit",        true,  nullptr,        1 },
    { L"WarnOnClose",        true,  nullptr,        0 },
    { L"TerminalType",       false, L"xterm",       0 },
    { L"TerminalSpeed",      false, L"38400,38400", 0 },
    { L"Font",               false, L"Consolas",    0 },
    { L"FontIsBold",         true,  nullptr,        0 },
    { L"FontCharSet",        true,  nullptr,        0 },
    { L"FontHeight",         true,  nullptr,        11 },
    { L"FontQuality",        true,  nullptr,        3 },
    { L"ScrollbackLines",    true,  nullptr,        20000 },
    { L"LineCodePage",       false, L"UTF-8",       0 },
    { L"BellStyle",          true,  nullptr,        0 },
    { L"SerialLine",         false, L"COM1",        0 },
    { L"SerialSpeed",        true,  nullptr,        9600 },
    { L"SerialDataBits",     true,  nullptr,        8 },
    { L"SerialStopHalfbits", true,  nullptr,        2 },
    { L"SerialParity",       true,  nullptr,        0 },
    { L"SerialFlowControl",  true,  nullptr,        0 },
};

// What a new session must say regardless of the template it came from.
struct Override
{
    std::wstring name;
    bool         isDword;
    std::wstring text;
    DWORD        number;
};

std::vector<Override> BuildOverrides(const Settings &settings,
                                     const std::wstring &portName, int speed)
{
    return {
        { L"Present",            true,  L"",       1 },
        { L"Protocol",           false, L"serial", 0 },
        { L"SerialLine",         false, portName,  0 },
        { L"SerialSpeed",        true,  L"",       (DWORD)speed },
        { L"SerialDataBits",     true,  L"",       (DWORD)settings.dataBits },
        { L"SerialStopHalfbits", true,  L"",       (DWORD)settings.stopHalfbits },
        { L"SerialParity",       true,  L"",       (DWORD)settings.parity },
        { L"SerialFlowControl",  true,  L"",       (DWORD)settings.flowControl },
    };
}

std::wstring NumberToText(DWORD value)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%lu", value);
    return std::wstring(buf);
}

// ---- directory backend ---------------------------------------------------

bool ReadWholeFile(const std::wstring &path, std::string &out)
{
    Util::Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.Valid()) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h.Get(), &size)) return false;
    if (size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024) return false;

    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    if (!ReadFile(h.Get(), &out[0], (DWORD)out.size(), &read, nullptr)) return false;
    out.resize(read);
    return true;
}

bool WriteWholeFile(const std::wstring &path, const std::string &data, std::wstring &error)
{
    Util::Handle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.Valid())
    {
        error = L"Could not create " + path + L"\n\n" + Util::FormatLastError(GetLastError());
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(h.Get(), data.data(), (DWORD)data.size(), &written, nullptr) ||
        written != data.size())
    {
        error = L"Could not write " + path + L"\n\n" + Util::FormatLastError(GetLastError());
        return false;
    }
    return true;
}

// Splits on LF and drops a trailing CR, so a file that picked up CRLF
// somewhere still round-trips as LF.
std::vector<std::string> SplitLines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos) ? text.substr(start)
                                                     : text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    // A trailing newline yields one empty tail element; drop it so we do not
    // accumulate blank lines on every rewrite.
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::string KeyOf(const std::string &line)
{
    size_t pos = line.find('\\');
    if (pos == std::string::npos) return std::string();
    return line.substr(0, pos);
}

std::string MakeLine(const std::string &key, const std::string &value)
{
    return key + "\\" + value + "\\";
}

std::string FallbackTemplateText()
{
    std::string out;
    for (const SessionValue &v : kFallbackTemplate)
    {
        const std::wstring text = v.isDword ? NumberToText(v.number) : std::wstring(v.text);
        out += MakeLine(Util::ToNarrow(v.name, CP_ACP), Util::ToNarrow(text, CP_ACP));
        out += '\n';
    }
    return out;
}

// Replaces each key's line in place, appending any key the template lacked.
std::string ApplyOverridesToText(const std::string &templateText,
                                 const std::vector<Override> &overrides)
{
    std::vector<std::string> lines = SplitLines(templateText);
    std::vector<bool> applied(overrides.size(), false);

    for (std::string &line : lines)
    {
        const std::string key = KeyOf(line);
        if (key.empty()) continue;
        for (size_t i = 0; i < overrides.size(); ++i)
        {
            const std::string name = Util::ToNarrow(overrides[i].name, CP_ACP);
            if (key != name) continue;
            const std::wstring text = overrides[i].isDword ? NumberToText(overrides[i].number)
                                                           : overrides[i].text;
            line = MakeLine(name, Util::ToNarrow(text, CP_ACP));
            applied[i] = true;
            break;
        }
    }

    for (size_t i = 0; i < overrides.size(); ++i)
    {
        if (applied[i]) continue;
        const std::wstring text = overrides[i].isDword ? NumberToText(overrides[i].number)
                                                       : overrides[i].text;
        lines.push_back(MakeLine(Util::ToNarrow(overrides[i].name, CP_ACP),
                                 Util::ToNarrow(text, CP_ACP)));
    }

    std::string out;
    for (const std::string &line : lines)
    {
        out += line;
        out += '\n';
    }
    return out;
}

bool EnsureInDirectory(const SessionStore::Resolved &where, const Settings &settings,
                       const std::wstring &portName, int speed,
                       bool &created, std::wstring &error)
{
    const std::wstring sessionName = SessionStore::NameFor(portName, speed);
    const std::wstring path =
        SessionStore::PathFor(where.sessionsDir, sessionName) + where.fileExtension;

    if (where.sessionsDir.empty())
    {
        error = L"Sessions are stored as files, but no sessions folder is configured.\n\n"
                L"Set one in Settings.";
        return false;
    }

    if (Util::FileExists(path)) return true;

    if (!Util::EnsureDirectory(where.sessionsDir))
    {
        error = L"The sessions folder does not exist and could not be created:\n" +
            where.sessionsDir;
        return false;
    }

    // Prefer the user's own "Default Settings" so a new session inherits their
    // colours, font and window preferences.
    std::string templateText;
    const std::wstring defaultsPath =
        SessionStore::PathFor(where.sessionsDir, L"Default Settings") + where.fileExtension;
    if (!ReadWholeFile(defaultsPath, templateText) || templateText.empty())
        templateText = FallbackTemplateText();

    const std::string content =
        ApplyOverridesToText(templateText, BuildOverrides(settings, portName, speed));

    if (!WriteWholeFile(path, content, error))
    {
        // Another launch may have won the race; that is a success for us.
        if (Util::FileExists(path)) { error.clear(); return true; }
        return false;
    }

    created = true;
    return true;
}

// ---- registry backend ----------------------------------------------------

std::wstring SessionKeyPath(const std::wstring &registryPath, const std::wstring &sessionName)
{
    return registryPath + L"\\" + Util::MungeSessionName(sessionName);
}

// A key with no values is a leftover rather than a real session, so treat it
// as absent and fill it in.
bool RegistrySessionExists(const std::wstring &registryPath, const std::wstring &sessionName)
{
    Util::RegKey key;
    const std::wstring path = SessionKeyPath(registryPath, sessionName);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, key.Receive())
            != ERROR_SUCCESS)
        return false;

    DWORD values = 0;
    if (RegQueryInfoKeyW(key.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &values, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        return false;

    return values > 0;
}

// Copies every value verbatim, preserving each one's type — a session mixes
// REG_SZ and REG_DWORD and the terminal cares which is which.
bool CopyRegistryValues(HKEY src, HKEY dst)
{
    DWORD valueCount = 0, maxNameLen = 0, maxDataLen = 0;
    if (RegQueryInfoKeyW(src, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &valueCount, &maxNameLen, &maxDataLen, nullptr, nullptr)
            != ERROR_SUCCESS)
        return false;

    std::vector<wchar_t> name(maxNameLen + 2);
    std::vector<BYTE> data(maxDataLen + 2);

    for (DWORD i = 0; i < valueCount; ++i)
    {
        DWORD nameLen = (DWORD)name.size();
        DWORD dataLen = (DWORD)data.size();
        DWORD type = 0;

        const LONG r = RegEnumValueW(src, i, name.data(), &nameLen, nullptr, &type,
                                     data.data(), &dataLen);
        if (r != ERROR_SUCCESS) continue;

        RegSetValueExW(dst, name.data(), 0, type, data.data(), dataLen);
    }
    return true;
}

void WriteFallbackValues(HKEY dst)
{
    for (const SessionValue &v : kFallbackTemplate)
    {
        if (v.isDword)
        {
            const DWORD number = v.number;
            RegSetValueExW(dst, v.name, 0, REG_DWORD,
                           reinterpret_cast<const BYTE *>(&number), sizeof(number));
        }
        else
        {
            const size_t bytes = (wcslen(v.text) + 1) * sizeof(wchar_t);
            RegSetValueExW(dst, v.name, 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(v.text), (DWORD)bytes);
        }
    }
}

bool EnsureInRegistry(const SessionStore::Resolved &where, const Settings &settings,
                      const std::wstring &portName, int speed,
                      bool &created, std::wstring &error)
{
    const std::wstring sessionName = SessionStore::NameFor(portName, speed);

    if (RegistrySessionExists(where.registryPath, sessionName)) return true;

    Util::RegKey dst;
    const std::wstring path = SessionKeyPath(where.registryPath, sessionName);
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                             dst.Receive(), nullptr);
    if (r != ERROR_SUCCESS)
    {
        error = L"Could not create the session key:\nHKEY_CURRENT_USER\\" + path + L"\n\n" +
            Util::FormatLastError((DWORD)r);
        return false;
    }

    // Inherit from "Default Settings" where the user has one, exactly as the
    // directory backend does.
    Util::RegKey defaults;
    const std::wstring defaultsPath = SessionKeyPath(where.registryPath, L"Default Settings");
    const bool haveDefaults =
        RegOpenKeyExW(HKEY_CURRENT_USER, defaultsPath.c_str(), 0, KEY_READ,
                      defaults.Receive()) == ERROR_SUCCESS;

    if (!haveDefaults || !CopyRegistryValues(defaults.Get(), dst.Get()))
        WriteFallbackValues(dst.Get());

    for (const Override &o : BuildOverrides(settings, portName, speed))
    {
        if (o.isDword)
        {
            const DWORD number = o.number;
            r = RegSetValueExW(dst.Get(), o.name.c_str(), 0, REG_DWORD,
                               reinterpret_cast<const BYTE *>(&number), sizeof(number));
        }
        else
        {
            const size_t bytes = (o.text.size() + 1) * sizeof(wchar_t);
            r = RegSetValueExW(dst.Get(), o.name.c_str(), 0, REG_SZ,
                               reinterpret_cast<const BYTE *>(o.text.c_str()), (DWORD)bytes);
        }
        if (r != ERROR_SUCCESS)
        {
            error = L"Could not write " + o.name + L" to\nHKEY_CURRENT_USER\\" + path +
                L"\n\n" + Util::FormatLastError((DWORD)r);
            return false;
        }
    }

    created = true;
    return true;
}

// Registry Editor reopens wherever it was last left, so pointing it at the key
// means writing that path into its own settings first.
void OpenRegistryEditorAt(const std::wstring &fullPath)
{
    Util::RegKey key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit",
                        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        key.Receive(), nullptr) == ERROR_SUCCESS)
    {
        const std::wstring lastKey = L"Computer\\" + fullPath;
        RegSetValueExW(key.Get(), L"LastKey", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(lastKey.c_str()),
                       (DWORD)((lastKey.size() + 1) * sizeof(wchar_t)));
    }
    ShellExecuteW(nullptr, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace

namespace SessionStore
{

std::wstring NameFor(const std::wstring &portName, int speed)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%s_%d", portName.c_str(), speed);
    return std::wstring(buf);
}

std::wstring PathFor(const std::wstring &sessionsDir, const std::wstring &sessionName)
{
    return Util::PathJoin(sessionsDir, Util::MungeSessionName(sessionName));
}

Resolved Resolve(const Settings &settings)
{
    Resolved out;
    out.registryPath = settings.registryPath;
    out.sessionsDir = settings.sessionsDir;

    const KittyConfig::Config config = KittyConfig::Detect(settings.terminalPath);
    out.iniPath = config.iniPath;
    out.fileExtension = config.fileExtension;

    if (Util::IEquals(settings.saveModeOverride, L"dir"))
    {
        out.mode = SaveMode::Directory;
    }
    else if (Util::IEquals(settings.saveModeOverride, L"registry"))
    {
        out.mode = SaveMode::Registry;
    }
    else
    {
        out.mode = config.saveMode;
        out.fromIni = true;
    }

    // Only fall back to kitty.ini's own idea of the folder when the user has
    // not named one; their explicit choice wins.
    if (out.sessionsDir.empty())
        out.sessionsDir = KittyConfig::SessionsDirFor(config, settings.terminalPath);

    return out;
}

bool EnsureExists(const Settings &settings, const std::wstring &portName, int speed,
                  bool &created, std::wstring &error)
{
    created = false;
    const Resolved where = Resolve(settings);

    switch (where.mode)
    {
    case SaveMode::Directory:
        return EnsureInDirectory(where, settings, portName, speed, created, error);

    case SaveMode::Registry:
        return EnsureInRegistry(where, settings, portName, speed, created, error);

    case SaveMode::SingleFile:
        error = L"kitty.ini asks for savemode=file, which KiTTY itself describes as "
                L"unmaintained and TermLaunch cannot write.\n\n" +
            where.iniPath +
            L"\n\nSet savemode=dir or savemode=registry there, or pin a mode in Settings.";
        return false;
    }

    error = L"Unknown session storage mode.";
    return false;
}

std::wstring DescribeLocation(const Settings &settings)
{
    const Resolved where = Resolve(settings);

    std::wstring out;
    switch (where.mode)
    {
    case SaveMode::Directory:
        out = L"Files in " + where.sessionsDir;
        if (!where.fileExtension.empty())
            out += L"  (extension " + where.fileExtension + L")";
        break;
    case SaveMode::Registry:
        out = L"Registry: HKEY_CURRENT_USER\\" + where.registryPath;
        break;
    case SaveMode::SingleFile:
        out = L"savemode=file — not supported";
        break;
    }

    if (where.fromIni)
    {
        out += where.iniPath.empty()
                   ? L"\n(no kitty.ini found, so KiTTY's default applies)"
                   : L"\n(from " + where.iniPath + L")";
    }
    else
    {
        out += L"\n(pinned in Settings)";
    }
    return out;
}

void OpenLocation(const Settings &settings)
{
    const Resolved where = Resolve(settings);

    if (where.mode == SaveMode::Registry)
    {
        OpenRegistryEditorAt(L"HKEY_CURRENT_USER\\" + where.registryPath);
        return;
    }

    if (where.sessionsDir.empty()) return;
    if (!Util::DirectoryExists(where.sessionsDir))
        Util::EnsureDirectory(where.sessionsDir);
    ShellExecuteW(nullptr, L"open", where.sessionsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring OpenLocationLabel(const Settings &settings)
{
    return Resolve(settings).mode == SaveMode::Registry
               ? L"Open sessions in Registry Editor"
               : L"Open sessions folder";
}

} // namespace SessionStore
