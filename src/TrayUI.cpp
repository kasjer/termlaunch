#include "TrayUI.h"
#include "SessionStore.h"
#include "Util.h"
#include "resource.h"

#include <shellapi.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace
{

const UINT kTrayIconId = 1;

// Frames of the arrival animation, as indices into TrayUI::m_icons. Ramps the
// green glow up and back down; 8 steps at 125 ms is one second.
const int  kPulseSequence[] = { 1, 2, 3, 4, 3, 2, 1, 0 };
const int  kPulseStepCount = (int)(sizeof(kPulseSequence) / sizeof(kPulseSequence[0]));

HICON LoadTrayIcon(HINSTANCE inst, int resourceId)
{
    // Ask for the small-icon metric so the shell gets the 16/20/24 px image
    // from the .ico rather than a downscaled 32 px one.
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HICON icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(resourceId), IMAGE_ICON,
                                   cx, cy, LR_DEFAULTCOLOR);
    if (!icon)
        icon = LoadIconW(inst, MAKEINTRESOURCEW(resourceId));
    return icon;
}

std::wstring FormatPortLabel(const Settings &settings, const SerialPort &port)
{
    std::wstring text = Util::EscapeMenuAmpersands(port.name);
    if (settings.showFriendlyName && !port.friendly.empty())
    {
        text += L"   \x00B7   ";   // middle dot
        text += Util::EscapeMenuAmpersands(port.friendly);
    }
    if (port.busy)
    {
        // Tab moves the rest into the menu's right-hand (accelerator) column.
        text += L"\tin use";
    }
    return text;
}

std::wstring FormatSpeed(int speed)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%d", speed);
    return std::wstring(buf);
}

} // namespace

// ---- lifetime ------------------------------------------------------------

bool TrayUI::Create(HINSTANCE inst, HWND hwnd)
{
    m_inst = inst;
    m_hwnd = hwnd;

    const int ids[5] = { IDI_APP, IDI_PULSE1, IDI_PULSE2, IDI_PULSE3, IDI_PULSE4 };
    for (int i = 0; i < 5; ++i)
    {
        m_icons[i] = LoadTrayIcon(inst, ids[i]);
    }
    if (!m_icons[0]) return false;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = m_icons[0];
    wcscpy_s(nid.szTip, L"TermLaunch");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    m_added = true;

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void TrayUI::Destroy()
{
    if (m_added)
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }
    for (HICON &icon : m_icons)
    {
        if (icon) { DestroyIcon(icon); icon = nullptr; }
    }
}

void TrayUI::Readd()
{
    m_added = false;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = m_icons[0];
    wcscpy_s(nid.szTip, L"TermLaunch");

    if (Shell_NotifyIconW(NIM_ADD, &nid))
    {
        m_added = true;
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

// ---- icon state ----------------------------------------------------------

void TrayUI::SetIcon(HICON icon)
{
    if (!m_added || !icon) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON;
    nid.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayUI::SetTooltip(const std::wstring &text)
{
    if (!m_added) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(nid.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayUI::BeginPulse()
{
    m_pulseStep = 0;
    SetIcon(m_icons[kPulseSequence[0]]);
}

bool TrayUI::OnPulseTick()
{
    if (m_pulseStep < 0) return false;

    ++m_pulseStep;
    if (m_pulseStep >= kPulseStepCount)
    {
        m_pulseStep = -1;
        SetIcon(m_icons[0]);
        return false;
    }

    const int frame = kPulseSequence[m_pulseStep];
    SetIcon(m_icons[frame] ? m_icons[frame] : m_icons[0]);
    return true;
}

// ---- menu ----------------------------------------------------------------

int TrayUI::ShowContextMenu(const Settings &settings,
                            const std::vector<SerialPort> &ports,
                            POINT anchor)
{
    Util::Menu menu(CreatePopupMenu());
    if (!menu.Get()) return 0;

    if (ports.empty())
    {
        AppendMenuW(menu.Get(), MF_STRING | MF_GRAYED, 0, L"No serial ports found");
    }
    else
    {
        for (size_t i = 0; i < ports.size() && i <= (IDM_PORT_LAST - IDM_PORT_FIRST); ++i)
        {
            const SerialPort &port = ports[i];
            UINT flags = MF_STRING;
            if (port.busy) flags |= MF_GRAYED;
            AppendMenuW(menu.Get(), flags, IDM_PORT_FIRST + (UINT)i,
                        FormatPortLabel(settings, port).c_str());
        }
    }

    AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);

    // Speed submenu. Ownership transfers to the parent on AppendMenu, so
    // release the guard once it is attached.
    Util::Menu speedMenu(CreatePopupMenu());
    if (speedMenu.Get())
    {
        int checkedPos = -1;
        for (size_t i = 0; i < settings.speeds.size() &&
             i <= (IDM_SPEED_LAST - IDM_SPEED_FIRST); ++i)
        {
            AppendMenuW(speedMenu.Get(), MF_STRING, IDM_SPEED_FIRST + (UINT)i,
                        FormatSpeed(settings.speeds[i]).c_str());
            if (settings.speeds[i] == settings.defaultSpeed) checkedPos = (int)i;
        }
        if (checkedPos >= 0)
        {
            CheckMenuRadioItem(speedMenu.Get(), 0,
                               (UINT)settings.speeds.size() - 1,
                               (UINT)checkedPos, MF_BYPOSITION);
        }

        const std::wstring label = L"Speed:  " + FormatSpeed(settings.defaultSpeed);
        AppendMenuW(menu.Get(), MF_POPUP, (UINT_PTR)speedMenu.Get(), label.c_str());
        speedMenu.Release();
    }

    AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.Get(), MF_STRING, IDM_REFRESH, L"Refresh");
    AppendMenuW(menu.Get(), MF_STRING, IDM_OPEN_SESSIONS,
                SessionStore::OpenLocationLabel(settings).c_str());
    AppendMenuW(menu.Get(), MF_STRING, IDM_SETTINGS, L"Settings\x2026");
    AppendMenuW(menu.Get(), MF_STRING, IDM_ABOUT, L"About TermLaunch");
    AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.Get(), MF_STRING, IDM_EXIT, L"Exit");

    // Without the foreground switch the menu will not dismiss when the user
    // clicks elsewhere; the WM_NULL afterwards is the documented companion.
    SetForegroundWindow(m_hwnd);

    UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

    const int cmd = (int)TrackPopupMenuEx(menu.Get(), flags, anchor.x, anchor.y,
                                          m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    return cmd;
}
