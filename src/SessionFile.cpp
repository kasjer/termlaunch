#include "SessionFile.h"
#include "Util.h"

#include <windows.h>
#include <vector>
#include <utility>

namespace
{

// Used when the sessions folder has no "Default Settings" to copy from.
// PuTTY supplies its own defaults for anything absent, so this stays short.
const char *const kFallbackTemplate =
    "Present\\1\\\n"
    "Protocol\\serial\\\n"
    "HostName\\\\\n"
    "PortNumber\\0\\\n"
    "CloseOnExit\\1\\\n"
    "WarnOnClose\\0\\\n"
    "TerminalType\\xterm\\\n"
    "TerminalSpeed\\38400,38400\\\n"
    "Font\\Consolas\\\n"
    "FontIsBold\\0\\\n"
    "FontCharSet\\0\\\n"
    "FontHeight\\11\\\n"
    "FontQuality\\3\\\n"
    "ScrollbackLines\\20000\\\n"
    "LineCodePage\\UTF-8\\\n"
    "BellStyle\\0\\\n"
    "SerialLine\\COM1\\\n"
    "SerialSpeed\\9600\\\n"
    "SerialDataBits\\8\\\n"
    "SerialStopHalfbits\\2\\\n"
    "SerialParity\\0\\\n"
    "SerialFlowControl\\0\\\n";

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

bool WriteWholeFile(const std::wstring &path, const std::string &data,
                    std::wstring &error)
{
    Util::Handle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.Valid())
    {
        error = L"Could not create " + path + L"\n\n" +
            Util::FormatLastError(GetLastError());
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(h.Get(), data.data(), (DWORD)data.size(), &written, nullptr) ||
        written != data.size())
    {
        error = L"Could not write " + path + L"\n\n" +
            Util::FormatLastError(GetLastError());
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
        std::string line = (nl == std::string::npos)
            ? text.substr(start)
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

std::string IntToNarrow(int value)
{
    char buf[32];
    sprintf_s(buf, "%d", value);
    return std::string(buf);
}

// Replaces each key's line in place, appending any key the template lacked.
std::string ApplyOverrides(const std::string &templateText,
                           const std::vector<std::pair<std::string, std::string>> &overrides)
{
    std::vector<std::string> lines = SplitLines(templateText);
    std::vector<bool> applied(overrides.size(), false);

    for (std::string &line : lines)
    {
        const std::string key = KeyOf(line);
        if (key.empty()) continue;
        for (size_t i = 0; i < overrides.size(); ++i)
        {
            if (key == overrides[i].first)
            {
                line = MakeLine(overrides[i].first, overrides[i].second);
                applied[i] = true;
                break;
            }
        }
    }

    for (size_t i = 0; i < overrides.size(); ++i)
    {
        if (!applied[i])
            lines.push_back(MakeLine(overrides[i].first, overrides[i].second));
    }

    std::string out;
    for (const std::string &line : lines)
    {
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace

namespace SessionFile
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

bool EnsureExists(const Settings &settings,
                  const std::wstring &portName,
                  int speed,
                  bool &created,
                  std::wstring &error)
{
    created = false;

    const std::wstring sessionName = NameFor(portName, speed);
    const std::wstring path = PathFor(settings.sessionsDir, sessionName);

    if (Util::FileExists(path)) return true;

    if (!Util::EnsureDirectory(settings.sessionsDir))
    {
        error = L"The sessions folder does not exist and could not be created:\n" +
            settings.sessionsDir;
        return false;
    }

    // Prefer the user's own "Default Settings" so a new session inherits their
    // colours, font and window preferences.
    std::string templateText;
    const std::wstring defaultsPath = PathFor(settings.sessionsDir, L"Default Settings");
    if (!ReadWholeFile(defaultsPath, templateText) || templateText.empty())
    {
        templateText = kFallbackTemplate;
    }

    const std::vector<std::pair<std::string, std::string>> overrides = {
        { "Present",            "1" },
        { "Protocol",           "serial" },
        { "WinTitle",           "%25%25s" },
        { "SerialLine",         Util::ToNarrow(portName, CP_ACP) },
        { "SerialSpeed",        IntToNarrow(speed) },
        { "SerialDataBits",     IntToNarrow(settings.dataBits) },
        { "SerialStopHalfbits", IntToNarrow(settings.stopHalfbits) },
        { "SerialParity",       IntToNarrow(settings.parity) },
        { "SerialFlowControl",  IntToNarrow(settings.flowControl) },
    };

    const std::string content = ApplyOverrides(templateText, overrides);

    if (!WriteWholeFile(path, content, error))
    {
        // Another launch may have won the race; that is a success for us.
        if (Util::FileExists(path)) { error.clear(); return true; }
        return false;
    }

    created = true;
    return true;
}

} // namespace SessionFile
