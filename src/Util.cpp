#include "Util.h"

#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "shlwapi.lib")

namespace Util
{

namespace
{

const wchar_t *const kAppTitle = L"TermLaunch";

int HexValue(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

} // namespace

// ---- strings -------------------------------------------------------------

std::wstring Trim(const std::wstring &s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool IEquals(const std::wstring &a, const std::wstring &b)
{
    return a.size() == b.size() &&
        CompareStringOrdinal(a.c_str(), (int)a.size(),
                             b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

bool StartsWithI(const std::wstring &s, const std::wstring &prefix)
{
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), (int)prefix.size(),
                                prefix.c_str(), (int)prefix.size(), TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> Split(const std::wstring &s, wchar_t sep)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true)
    {
        size_t pos = s.find(sep, start);
        if (pos == std::wstring::npos)
        {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::wstring Join(const std::vector<std::wstring> &parts, const std::wstring &sep)
{
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::wstring EscapeMenuAmpersands(const std::wstring &s)
{
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t c : s)
    {
        out += c;
        if (c == L'&') out += c;
    }
    return out;
}

std::string ToNarrow(const std::wstring &s, UINT codePage)
{
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(codePage, 0, s.c_str(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(codePage, 0, s.c_str(), (int)s.size(),
                        &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string &s, UINT codePage)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(codePage, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(codePage, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// ---- PuTTY session-name encoding -----------------------------------------

std::wstring MungeSessionName(const std::wstring &name)
{
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(name.size() + 8);
    bool canDot = false;   // a leading '.' is escaped; later ones are not
    for (wchar_t c : name)
    {
        const bool special =
            c == L' ' || c == L'\\' || c == L'*' || c == L'?' || c == L'%' ||
            c < L' ' || c > L'~' || (c == L'.' && !canDot);
        if (special)
        {
            unsigned v = (unsigned)(c & 0xFF);
            out += L'%';
            out += kHex[(v >> 4) & 0xF];
            out += kHex[v & 0xF];
        }
        else
        {
            out += c;
        }
        canDot = true;
    }
    return out;
}

std::wstring UnmungeSessionName(const std::wstring &fileName)
{
    std::wstring out;
    out.reserve(fileName.size());
    for (size_t i = 0; i < fileName.size(); ++i)
    {
        if (fileName[i] == L'%' && i + 2 < fileName.size())
        {
            int hi = HexValue(fileName[i + 1]);
            int lo = HexValue(fileName[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += (wchar_t)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += fileName[i];
    }
    return out;
}

// ---- paths ---------------------------------------------------------------

std::wstring GetExePath()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
        if (n == 0) return std::wstring();
        if (n < buf.size()) { buf.resize(n); return buf; }
        buf.resize(buf.size() * 2);   // truncated; grow and retry
    }
}

std::wstring GetExeDir()
{
    return GetDirectoryOf(GetExePath());
}

std::wstring GetDirectoryOf(const std::wstring &filePath)
{
    size_t pos = filePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return filePath.substr(0, pos);
}

std::wstring PathJoin(const std::wstring &a, const std::wstring &b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out += L'\\';
    size_t start = 0;
    while (start < b.size() && (b[start] == L'\\' || b[start] == L'/')) ++start;
    out += b.substr(start);
    return out;
}

bool FileExists(const std::wstring &path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring &path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirectory(const std::wstring &path)
{
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;
    std::wstring parent = GetDirectoryOf(path);
    if (!parent.empty() && parent != path && !DirectoryExists(parent))
    {
        if (!EnsureDirectory(parent)) return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool IsDirectoryWritable(const std::wstring &dir)
{
    if (!DirectoryExists(dir)) return false;
    // Only an actual create proves it: the directory can be read-only, on
    // read-only media, or virtualised.
    std::wstring probe = PathJoin(dir, L"termlaunch.write-test.tmp");
    Handle h(CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                         nullptr));
    return h.Valid();
}

// ---- misc ----------------------------------------------------------------

std::wstring FormatLastError(DWORD err)
{
    LPWSTR msg = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, nullptr);
    std::wstring out;
    if (n && msg)
    {
        out.assign(msg, n);
        out = Trim(out);
    }
    LocalFree(msg);
    if (out.empty())
    {
        wchar_t buf[64];
        swprintf_s(buf, L"error %lu", err);
        out = buf;
    }
    return out;
}

void ShowError(HWND owner, const std::wstring &text)
{
    MessageBoxW(owner, text.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

void ShowInfo(HWND owner, const std::wstring &text)
{
    MessageBoxW(owner, text.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
}

bool AskYesNo(HWND owner, const std::wstring &text)
{
    return MessageBoxW(owner, text.c_str(), kAppTitle,
                       MB_YESNO | MB_ICONQUESTION) == IDYES;
}

} // namespace Util
