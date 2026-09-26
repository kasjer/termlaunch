// Small helpers shared across TermLaunch: RAII wrappers for Win32 handles,
// path and string utilities, and the PuTTY session-name encoding.

#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace Util
{

// ---- RAII ----------------------------------------------------------------

// Owns a HANDLE whose "empty" value may be either NULL or INVALID_HANDLE_VALUE.
class Handle
{
public:
    Handle() = default;
    explicit Handle(HANDLE h) : m_h(h) {}
    ~Handle() { Reset(); }

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    Handle(Handle &&o) noexcept : m_h(o.m_h) { o.m_h = nullptr; }
    Handle &operator=(Handle &&o) noexcept
    {
        if (this != &o) { Reset(); m_h = o.m_h; o.m_h = nullptr; }
        return *this;
    }

    void Reset(HANDLE h = nullptr)
    {
        if (m_h && m_h != INVALID_HANDLE_VALUE) { ::CloseHandle(m_h); }
        m_h = h;
    }
    bool Valid() const { return m_h && m_h != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return m_h; }

private:
    HANDLE m_h = nullptr;
};

class RegKey
{
public:
    RegKey() = default;
    ~RegKey() { Reset(); }

    RegKey(const RegKey &) = delete;
    RegKey &operator=(const RegKey &) = delete;

    void Reset(HKEY k = nullptr)
    {
        if (m_k) { ::RegCloseKey(m_k); }
        m_k = k;
    }
    bool Valid() const { return m_k != nullptr; }
    HKEY Get() const { return m_k; }
    HKEY *Receive() { Reset(); return &m_k; }

private:
    HKEY m_k = nullptr;
};

class Menu
{
public:
    Menu() = default;
    explicit Menu(HMENU m) : m_m(m) {}
    ~Menu() { Reset(); }

    Menu(const Menu &) = delete;
    Menu &operator=(const Menu &) = delete;

    void Reset(HMENU m = nullptr)
    {
        if (m_m) { ::DestroyMenu(m_m); }
        m_m = m;
    }
    HMENU Get() const { return m_m; }
    HMENU Release() { HMENU m = m_m; m_m = nullptr; return m; }

private:
    HMENU m_m = nullptr;
};

// ---- strings -------------------------------------------------------------

std::wstring Trim(const std::wstring &s);
bool IEquals(const std::wstring &a, const std::wstring &b);
bool StartsWithI(const std::wstring &s, const std::wstring &prefix);
std::vector<std::wstring> Split(const std::wstring &s, wchar_t sep);
std::wstring Join(const std::vector<std::wstring> &parts, const std::wstring &sep);

// Doubles every '&' so the text survives TrackPopupMenu's mnemonic parsing.
// Driver-supplied friendly names really do contain '&'.
std::wstring EscapeMenuAmpersands(const std::wstring &s);

// Narrow<->wide using the given code page (CP_UTF8 or CP_ACP).
std::string  ToNarrow(const std::wstring &s, UINT codePage);
std::wstring ToWide(const std::string &s, UINT codePage);

// ---- PuTTY / KiTTY session-name encoding ---------------------------------
//
// PuTTY's mungestr(): a session name becomes a file name by percent-escaping
// space, '\', '*', '?', '%', anything outside printable ASCII, and a leading
// '.'. "Default Settings" -> "Default%20Settings".
std::wstring MungeSessionName(const std::wstring &name);
std::wstring UnmungeSessionName(const std::wstring &fileName);

// ---- paths ---------------------------------------------------------------

std::wstring GetExePath();
std::wstring GetExeDir();
std::wstring GetDirectoryOf(const std::wstring &filePath);
std::wstring PathJoin(const std::wstring &a, const std::wstring &b);
bool FileExists(const std::wstring &path);
bool DirectoryExists(const std::wstring &path);
bool EnsureDirectory(const std::wstring &path);
// True when a new file can actually be created in `dir`.
bool IsDirectoryWritable(const std::wstring &dir);

// ---- misc ----------------------------------------------------------------

std::wstring FormatLastError(DWORD err);
void ShowError(HWND owner, const std::wstring &text);
void ShowInfo(HWND owner, const std::wstring &text);
bool AskYesNo(HWND owner, const std::wstring &text);

} // namespace Util
