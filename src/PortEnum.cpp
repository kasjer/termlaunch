#include "PortEnum.h"
#include "Util.h"

#include <windows.h>
#include <setupapi.h>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{

// Defined inline rather than pulled from devguid.h / ntddser.h so the project
// needs neither initguid.h ordering games nor uuid.lib.
// {4D36E978-E325-11CE-BFC1-08002BE10318}
const GUID kPortsClassGuid =
{ 0x4D36E978, 0xE325, 0x11CE, { 0xBF,0xC1,0x08,0x00,0x2B,0xE1,0x03,0x18 } };

std::wstring ReadDevRegString(HKEY key, const wchar_t *value)
{
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
        return std::wstring();
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::wstring();

    std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, value, nullptr, &type,
                         reinterpret_cast<BYTE *>(&buf[0]), &cb) != ERROR_SUCCESS)
        return std::wstring();

    buf.resize(wcslen(buf.c_str()));
    return buf;
}

std::wstring ReadDeviceProperty(HDEVINFO devInfo, SP_DEVINFO_DATA &data, DWORD prop)
{
    DWORD type = 0, cb = 0;
    SetupDiGetDeviceRegistryPropertyW(devInfo, &data, prop, &type, nullptr, 0, &cb);
    if (cb == 0) return std::wstring();

    std::vector<BYTE> buf(cb + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &data, prop, &type,
                                           buf.data(), cb, nullptr))
        return std::wstring();

    return std::wstring(reinterpret_cast<wchar_t *>(buf.data()));
}

// "Silicon Labs CP210x USB to UART Bridge (COM4)" -> the same without the
// trailing "(COM4)", which the menu already shows in its own column.
std::wstring StripTrailingPortSuffix(const std::wstring &friendly,
                                     const std::wstring &portName)
{
    std::wstring suffix = L" (" + portName + L")";
    if (friendly.size() > suffix.size() &&
        Util::IEquals(friendly.substr(friendly.size() - suffix.size()), suffix))
    {
        return friendly.substr(0, friendly.size() - suffix.size());
    }
    return friendly;
}

int ParsePortNumber(const std::wstring &name)
{
    if (!Util::StartsWithI(name, L"COM")) return 0;
    int n = 0;
    for (size_t i = 3; i < name.size(); ++i)
    {
        if (name[i] < L'0' || name[i] > L'9') return 0;
        n = n * 10 + (name[i] - L'0');
    }
    return n;
}

// Ports registered in the device map but with no device node we could read —
// some virtual/legacy drivers land here. Used only to top up the SetupAPI list.
void AddDeviceMapPorts(std::vector<SerialPort> &ports)
{
    Util::RegKey key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, key.Receive()) != ERROR_SUCCESS)
        return;

    for (DWORD i = 0;; ++i)
    {
        wchar_t nameBuf[512];
        DWORD nameLen = ARRAYSIZE(nameBuf);
        BYTE  dataBuf[512];
        DWORD dataLen = sizeof(dataBuf);
        DWORD type = 0;

        LONG r = RegEnumValueW(key.Get(), i, nameBuf, &nameLen, nullptr,
                               &type, dataBuf, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        if (type != REG_SZ) continue;

        std::wstring portName(reinterpret_cast<wchar_t *>(dataBuf),
                              dataLen / sizeof(wchar_t));
        portName.resize(wcslen(portName.c_str()));
        if (!Util::StartsWithI(portName, L"COM")) continue;

        const bool known = std::any_of(
            ports.begin(), ports.end(),
            [&](const SerialPort &p) { return Util::IEquals(p.name, portName); });
        if (known) continue;

        SerialPort p;
        p.name = portName;
        p.number = ParsePortNumber(portName);
        p.friendly = L"Serial port";
        ports.push_back(p);
    }
}

} // namespace

namespace Ports
{

std::vector<SerialPort> Enumerate()
{
    std::vector<SerialPort> ports;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&kPortsClassGuid, nullptr, nullptr,
                                            DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVINFO_DATA data = {};
        data.cbSize = sizeof(data);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i)
        {
            HKEY raw = SetupDiOpenDevRegKey(devInfo, &data, DICS_FLAG_GLOBAL, 0,
                                            DIREG_DEV, KEY_READ);
            if (raw == nullptr || raw == reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE))
                continue;

            Util::RegKey devKey;
            devKey.Reset(raw);

            std::wstring portName = ReadDevRegString(devKey.Get(), L"PortName");
            // The Ports class also holds LPT devices.
            if (!Util::StartsWithI(portName, L"COM")) continue;

            std::wstring friendly = ReadDeviceProperty(devInfo, data, SPDRP_FRIENDLYNAME);
            if (friendly.empty())
                friendly = ReadDeviceProperty(devInfo, data, SPDRP_DEVICEDESC);

            SerialPort p;
            p.name = portName;
            p.number = ParsePortNumber(portName);
            p.friendly = StripTrailingPortSuffix(friendly, portName);
            ports.push_back(p);
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    AddDeviceMapPorts(ports);

    // COM9 must come before COM10, so sort on the number, not the string.
    std::sort(ports.begin(), ports.end(),
              [](const SerialPort &a, const SerialPort &b) {
                  if (a.number != b.number) return a.number < b.number;
                  return a.name < b.name;
              });
    return ports;
}

bool ProbeBusy(const std::wstring &portName)
{
    // The \\.\ prefix is required for COM10 and above.
    std::wstring path = L"\\\\.\\" + portName;

    // dwDesiredAccess 0 asks only for device metadata, which keeps the driver
    // from doing read/write setup. It still goes through the driver's open
    // path, which is why this runs only on demand.
    Util::Handle h(CreateFileW(path.c_str(), 0, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (h.Valid()) return false;

    const DWORD err = GetLastError();
    // ERROR_SHARING_VIOLATION shows up for some virtual-port drivers.
    return err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION;
}

void ProbeBusy(std::vector<SerialPort> &ports)
{
    for (SerialPort &p : ports)
    {
        p.busy = ProbeBusy(p.name);
    }
}

std::vector<std::wstring> NamesOf(const std::vector<SerialPort> &ports)
{
    std::vector<std::wstring> names;
    names.reserve(ports.size());
    for (const SerialPort &p : ports) names.push_back(p.name);
    return names;
}

} // namespace Ports
