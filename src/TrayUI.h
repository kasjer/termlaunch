// Notification-area icon, its context menu, and the new-port animation.

#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "PortEnum.h"
#include "Settings.h"

// Private window message carrying notification-area callbacks.
#define WM_APP_TRAY (WM_APP + 1)

class TrayUI
{
public:
    bool Create(HINSTANCE inst, HWND hwnd);
    void Destroy();

    // Re-adds the icon after Explorer restarts (TaskbarCreated).
    void Readd();

    // Builds and tracks the menu at `anchor` (screen coordinates), returning
    // the chosen command id, or 0 if the menu was dismissed. Uses
    // TPM_RETURNCMD, so nothing routes through WM_COMMAND.
    int ShowContextMenu(const Settings &settings,
                        const std::vector<SerialPort> &ports,
                        POINT anchor);

    void SetTooltip(const std::wstring &text);

    // ~1 second of icon animation; see kPulseIntervalMs / the frame sequence
    // in the .cpp. Safe to call while a previous run is still going.
    void BeginPulse();
    // Advances one frame. Returns false once the animation is finished.
    bool OnPulseTick();
    bool PulseActive() const { return m_pulseStep >= 0; }

    HICON AppIcon() const { return m_icons[0]; }

private:
    void SetIcon(HICON icon);

    HINSTANCE m_inst = nullptr;
    HWND      m_hwnd = nullptr;
    bool      m_added = false;

    // [0] is the idle icon, [1..4] the pulse frames.
    HICON m_icons[5] = {};
    int   m_pulseStep = -1;
};
