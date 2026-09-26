// Portable configuration for TermLaunch.
//
// Stored in termlaunch.ini next to the executable, or in
// %APPDATA%\TermLaunch\termlaunch.ini when the executable's directory is not
// writable (read-only media, Program Files).

#pragma once

#include <string>
#include <vector>

struct Settings
{
    std::wstring terminalPath  = L"C:\\Users\\Jerzy\\Downloads\\kitty_portable.exe";
    std::wstring sessionsDir   = L"C:\\Users\\Jerzy\\Downloads\\Sessions";

    int defaultSpeed = 1000000;
    std::vector<int> speeds = { 9600, 19200, 38400, 57600, 115200,
                                230400, 460800, 921600, 1000000, 2000000 };

    // Serial line defaults written into newly created session files.
    int dataBits     = 8;
    int stopHalfbits = 2;   // 2 half-bits = 1 stop bit
    int parity       = 0;   // 0 none, 1 odd, 2 even, 3 mark, 4 space
    int flowControl  = 0;   // 0 none, 1 XON/XOFF, 2 RTS/CTS, 3 DSR/DTR

    bool detectBusy       = true;   // grey out ports another process holds open
    bool animateOnArrival = true;   // ~1 s icon animation when a port appears
    bool showFriendlyName = true;   // show the Device Manager description

    // Where this instance reads and writes its INI. Set by Load().
    std::wstring iniPath;

    void Load();
    bool Save(std::wstring &error) const;

    std::wstring SpeedsAsText() const;            // "9600,115200,1000000"
    void SetSpeedsFromText(const std::wstring &text);

    bool Validate(std::wstring &problem) const;
};

namespace AutoStart
{

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value "TermLaunch".
bool IsEnabled();
bool SetEnabled(bool enable, std::wstring &error);

// Rewrites the Run value when it exists but points somewhere else — keeps
// auto-start working after the portable exe is moved.
void RefreshPathIfEnabled();

} // namespace AutoStart
