#include "KittyConfig.h"
#include "Util.h"

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{

// kitty.ini files in the wild use both comment characters: the one KiTTY ships
// into %APPDATA% comments with ';', the one written next to a portable exe
// uses '#'. Treat either as a comment, or savemode lines that are switched off
// get read as if they were live.
bool IsCommentOrBlank(const std::wstring &line)
{
    return line.empty() || line[0] == L';' || line[0] == L'#';
}

std::wstring ToLower(const std::wstring &s)
{
    std::wstring out = s;
    for (wchar_t &c : out) c = (wchar_t)towlower(c);
    return out;
}

// Strips one layer of surrounding quotes, which configdir values sometimes use.
std::wstring Unquote(const std::wstring &s)
{
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        return s.substr(1, s.size() - 2);
    return s;
}

bool ReadTextFile(const std::wstring &path, std::string &out)
{
    Util::Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.Valid()) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h.Get(), &size)) return false;
    if (size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) return false;

    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    if (!ReadFile(h.Get(), &out[0], (DWORD)out.size(), &read, nullptr)) return false;
    out.resize(read);
    return true;
}

std::wstring AppDataKittyIni()
{
    PWSTR roaming = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        path = Util::PathJoin(Util::PathJoin(roaming, L"KiTTY"), L"kitty.ini");
        CoTaskMemFree(roaming);
    }
    return path;
}

SaveMode ParseSaveMode(const std::wstring &value, bool &recognised)
{
    const std::wstring v = ToLower(Util::Trim(value));
    recognised = true;
    if (v == L"dir")      return SaveMode::Directory;
    if (v == L"registry") return SaveMode::Registry;
    if (v == L"file")     return SaveMode::SingleFile;
    recognised = false;
    return SaveMode::Registry;
}

// Fills in the savemode / configdir / fileextension fields from an ini's text.
void ParseInto(const std::string &text, KittyConfig::Config &config)
{
    std::wstring section;

    for (const std::wstring &rawLine : Util::Split(Util::ToWide(text, CP_ACP), L'\n'))
    {
        const std::wstring line = Util::Trim(rawLine);
        if (IsCommentOrBlank(line)) continue;

        if (line.front() == L'[')
        {
            const size_t end = line.find(L']');
            if (end != std::wstring::npos)
                section = ToLower(line.substr(1, end - 1));
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        const std::wstring key = ToLower(Util::Trim(line.substr(0, eq)));
        const std::wstring value = Util::Trim(line.substr(eq + 1));

        // These all live under [KiTTY]; accept them anywhere rather than
        // silently ignoring a hand-edited file with a missing section header.
        if (!section.empty() && section != L"kitty") continue;

        if (key == L"savemode")
        {
            bool recognised = false;
            const SaveMode mode = ParseSaveMode(value, recognised);
            if (recognised)
            {
                config.saveMode = mode;
                config.saveModeStated = true;
            }
        }
        else if (key == L"configdir")
        {
            config.configDir = Unquote(value);
        }
        else if (key == L"fileextension")
        {
            config.fileExtension = Unquote(value);
        }
    }
}

} // namespace

namespace KittyConfig
{

Config Detect(const std::wstring &terminalPath)
{
    Config config;

    std::vector<std::wstring> candidates;
    // Portable mode wins: a kitty.ini next to the executable is the one that
    // exe will read, whatever %APPDATA% happens to contain.
    const std::wstring exeDir = Util::GetDirectoryOf(terminalPath);
    if (!exeDir.empty())
        candidates.push_back(Util::PathJoin(exeDir, L"kitty.ini"));

    const std::wstring appData = AppDataKittyIni();
    if (!appData.empty())
        candidates.push_back(appData);

    for (const std::wstring &candidate : candidates)
    {
        std::string text;
        if (!Util::FileExists(candidate)) continue;
        if (!ReadTextFile(candidate, text)) continue;

        config.found = true;
        config.iniPath = candidate;
        ParseInto(text, config);
        break;
    }

    return config;
}

std::wstring SessionsDirFor(const Config &config, const std::wstring &terminalPath)
{
    std::wstring base = config.configDir;
    if (base.empty())
        base = Util::GetDirectoryOf(terminalPath);
    if (base.empty())
        return std::wstring();
    return Util::PathJoin(base, L"Sessions");
}

const wchar_t *SaveModeName(SaveMode mode)
{
    switch (mode)
    {
    case SaveMode::Directory:  return L"files in a folder";
    case SaveMode::Registry:   return L"Windows registry";
    case SaveMode::SingleFile: return L"single file";
    }
    return L"unknown";
}

} // namespace KittyConfig
