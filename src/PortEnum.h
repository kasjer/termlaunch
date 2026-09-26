// Enumeration of the COM ports currently present, plus the "is it already
// open?" probe used to grey out busy ports in the tray menu.

#pragma once

#include <string>
#include <vector>

struct SerialPort
{
    std::wstring name;        // "COM4"
    int          number = 0;  // 4 — used for numeric sorting (COM9 before COM10)
    std::wstring friendly;    // Device Manager text, minus its trailing "(COM4)"
    bool         busy = false;
};

namespace Ports
{

// Ports with a device node present right now, sorted by number.
// Never probes whether a port is open — see ProbeBusy for that.
std::vector<SerialPort> Enumerate();

// True when the port exists but another process holds it open.
//
// Opening the port is the only reliable signal, and opening a serial port can
// assert DTR/RTS and reset an attached board. Call this only in response to a
// user action (menu build), never on a timer, and honour Settings::detectBusy.
bool ProbeBusy(const std::wstring &portName);

// Fills in `busy` for every entry. Same caveat as ProbeBusy.
void ProbeBusy(std::vector<SerialPort> &ports);

// Just the names, for cheap change detection between polls.
std::vector<std::wstring> NamesOf(const std::vector<SerialPort> &ports);

} // namespace Ports
