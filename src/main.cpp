// TermLaunch — a notification-area launcher for serial terminals.
//
// Keeps no visible window: a hidden top-level window owns the tray icon and
// receives device-change broadcasts. See CLAUDE.md for the design notes.

#include <windows.h>
#include <objbase.h>     // WIN32_LEAN_AND_MEAN leaves COM out of windows.h
#include <dbt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>
#include <string>
#include <vector>

#include "Launcher.h"
#include "PortEnum.h"
#include "SessionFile.h"
#include "Settings.h"
#include "SettingsDlg.h"
#include "TrayUI.h"
#include "Util.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace
{

const wchar_t *const kWindowClass = L"TermLaunch.Hidden.Window";
const wchar_t *const kMutexName = L"Local\\TermLaunch.SingleInstance.v1";

// Asking an already-running instance to pop its menu.
#define WM_APP_SHOWMENU (WM_APP + 2)

enum : UINT_PTR
{
    TIMER_POLL = 1,   // periodic safety net for missed device notifications
    TIMER_RESCAN = 2,   // debounce after a WM_DEVICECHANGE burst
    TIMER_PULSE = 3,   // arrival animation frames
};

const UINT kPollIntervalMs = 2000;
const UINT kRescanDelayMs = 400;
const UINT kPulseIntervalMs = 125;   // x 8 frames = ~1 second

// GUID_DEVINTERFACE_COMPORT {86E0D1E0-8089-11D0-9CE4-08003E301F73}
const GUID kComPortInterfaceGuid =
{ 0x86E0D1E0, 0x8089, 0x11D0, { 0x9C,0xE4,0x08,0x00,0x3E,0x30,0x1F,0x73 } };

struct App
{
    HINSTANCE inst = nullptr;
    HWND      hwnd = nullptr;
    Settings  settings;
    TrayUI    tray;
    HDEVNOTIFY deviceNotify = nullptr;
    UINT      taskbarCreatedMsg = 0;
    std::vector<std::wstring> knownPorts;   // names seen at the last scan
};

App g_app;

std::wstring BuildTooltip(size_t portCount)
{
    wchar_t buf[128];
    if (portCount == 0)
        swprintf_s(buf, L"TermLaunch \x2014 no serial ports");
    else if (portCount == 1)
        swprintf_s(buf, L"TermLaunch \x2014 1 serial port");
    else
        swprintf_s(buf, L"TermLaunch \x2014 %zu serial ports", portCount);
    return std::wstring(buf);
}

// Refreshes the cached port list. When `announceArrivals` is set and a name
// that was not there before shows up, kicks off the tray animation.
void Rescan(bool announceArrivals)
{
    std::vector<std::wstring> names = Ports::NamesOf(Ports::Enumerate());

    bool arrived = false;
    for (const std::wstring &name : names)
    {
        const bool seenBefore = std::any_of(
            g_app.knownPorts.begin(), g_app.knownPorts.end(),
            [&](const std::wstring &known) { return Util::IEquals(known, name); });
        if (!seenBefore) { arrived = true; break; }
    }

    const bool changed = names.size() != g_app.knownPorts.size() || arrived;
    g_app.knownPorts = std::move(names);

    if (changed)
        g_app.tray.SetTooltip(BuildTooltip(g_app.knownPorts.size()));

    if (arrived && announceArrivals && g_app.settings.animateOnArrival)
    {
        g_app.tray.BeginPulse();
        SetTimer(g_app.hwnd, TIMER_PULSE, kPulseIntervalMs, nullptr);
    }
}

void ShowAbout(HWND owner)
{
    const std::wstring text =
        L"TermLaunch 1.0\n"
        L"Serial port launcher for KiTTY and PuTTY.\n\n"
        L"Terminal:\n" + g_app.settings.terminalPath + L"\n\n"
        L"Sessions:\n" + g_app.settings.sessionsDir + L"\n\n"
        L"Settings file:\n" + g_app.settings.iniPath;
    Util::ShowInfo(owner, text);
}

void OnPortChosen(const SerialPort &port)
{
    const int speed = g_app.settings.defaultSpeed;

    std::wstring error;
    if (Launcher::Launch(g_app.settings, port.name, speed, error))
        return;

    // The usual cause is an unconfigured or moved terminal, so offer the way out.
    const std::wstring msg = error + L"\n\nOpen settings now?";
    if (Util::AskYesNo(g_app.hwnd, msg))
        SettingsDlg::Show(g_app.hwnd, g_app.inst, g_app.settings);
}

void OnSpeedChosen(size_t index)
{
    if (index >= g_app.settings.speeds.size()) return;
    g_app.settings.defaultSpeed = g_app.settings.speeds[index];

    std::wstring error;
    if (!g_app.settings.Save(error))
        Util::ShowError(g_app.hwnd, error);
}

void ShowTrayMenu(POINT anchor)
{
    std::vector<SerialPort> ports = Ports::Enumerate();
    if (g_app.settings.detectBusy)
        Ports::ProbeBusy(ports);

    const int cmd = g_app.tray.ShowContextMenu(g_app.settings, ports, anchor);
    if (cmd == 0) return;

    if (cmd >= IDM_PORT_FIRST && cmd <= IDM_PORT_LAST)
    {
        const size_t index = (size_t)(cmd - IDM_PORT_FIRST);
        if (index < ports.size()) OnPortChosen(ports[index]);
        return;
    }
    if (cmd >= IDM_SPEED_FIRST && cmd <= IDM_SPEED_LAST)
    {
        OnSpeedChosen((size_t)(cmd - IDM_SPEED_FIRST));
        return;
    }

    switch (cmd)
    {
    case IDM_REFRESH:
        Rescan(false);
        break;
    case IDM_OPEN_SESSIONS:
        if (!Util::DirectoryExists(g_app.settings.sessionsDir))
            Util::EnsureDirectory(g_app.settings.sessionsDir);
        Launcher::OpenFolder(g_app.settings.sessionsDir);
        break;
    case IDM_SETTINGS:
        SettingsDlg::Show(g_app.hwnd, g_app.inst, g_app.settings);
        break;
    case IDM_ABOUT:
        ShowAbout(g_app.hwnd);
        break;
    case IDM_EXIT:
        DestroyWindow(g_app.hwnd);
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_app.taskbarCreatedMsg && g_app.taskbarCreatedMsg != 0)
    {
        g_app.tray.Readd();
        g_app.tray.SetTooltip(BuildTooltip(g_app.knownPorts.size()));
        return 0;
    }

    switch (msg)
    {
    case WM_APP_TRAY:
        // With NOTIFYICON_VERSION_4 the event is in the low word of lParam and
        // the anchor point is in wParam — which is where the icon is, even when
        // the icon was activated from the keyboard.
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU: {
            POINT anchor = { GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) };
            ShowTrayMenu(anchor);
            break;
        }
        }
        return 0;

    case WM_APP_SHOWMENU: {
        POINT anchor = {};
        GetCursorPos(&anchor);
        ShowTrayMenu(anchor);
        return 0;
    }

    case WM_DEVICECHANGE:
        // Device-interface arrivals and the generic devnode broadcast both
        // matter; coalesce the burst into one rescan.
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE ||
            wParam == DBT_DEVNODES_CHANGED)
        {
            SetTimer(hwnd, TIMER_RESCAN, kRescanDelayMs, nullptr);
        }
        return TRUE;

    case WM_TIMER:
        switch (wParam)
        {
        case TIMER_RESCAN:
            KillTimer(hwnd, TIMER_RESCAN);
            Rescan(true);
            break;
        case TIMER_POLL:
            Rescan(true);
            break;
        case TIMER_PULSE:
            if (!g_app.tray.OnPulseTick())
                KillTimer(hwnd, TIMER_PULSE);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterWindowClass(HINSTANCE inst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

bool RegisterForDeviceNotifications(HWND hwnd)
{
    DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = kComPortInterfaceGuid;

    g_app.deviceNotify = RegisterDeviceNotificationW(
        hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    return g_app.deviceNotify != nullptr;
}

} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    // A second copy just surfaces the first one's menu.
    Util::Handle mutex(CreateMutexW(nullptr, FALSE, kMutexName));
    if (mutex.Valid() && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) PostMessageW(existing, WM_APP_SHOWMENU, 0, 0);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    g_app.inst = inst;
    g_app.settings.Load();
    AutoStart::RefreshPathIfEnabled();

    if (!RegisterWindowClass(inst))
    {
        Util::ShowError(nullptr, L"Could not register the application window class.");
        return 1;
    }

    // A real top-level window, not HWND_MESSAGE: message-only windows do not
    // receive the DBT_DEVNODES_CHANGED broadcast. It is simply never shown.
    g_app.hwnd = CreateWindowExW(0, kWindowClass, L"TermLaunch", WS_OVERLAPPED,
                                 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_app.hwnd)
    {
        Util::ShowError(nullptr, L"Could not create the application window.");
        return 1;
    }

    g_app.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    if (!g_app.tray.Create(inst, g_app.hwnd))
    {
        Util::ShowError(nullptr, L"Could not add the notification-area icon.");
        return 1;
    }

    RegisterForDeviceNotifications(g_app.hwnd);

    // Seed the known-port list so existing ports do not look like arrivals.
    g_app.knownPorts = Ports::NamesOf(Ports::Enumerate());
    g_app.tray.SetTooltip(BuildTooltip(g_app.knownPorts.size()));

    SetTimer(g_app.hwnd, TIMER_POLL, kPollIntervalMs, nullptr);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_app.deviceNotify) UnregisterDeviceNotification(g_app.deviceNotify);
    g_app.tray.Destroy();
    CoUninitialize();
    return (int)message.wParam;
}
