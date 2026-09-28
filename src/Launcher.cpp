#include "Launcher.h"
#include "SessionStore.h"
#include "Util.h"

#include <windows.h>
#include <shellapi.h>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace Launcher
{

bool Launch(const Settings &settings,
            const std::wstring &portName,
            int speed,
            std::wstring &error)
{
    std::wstring problem;
    if (!settings.Validate(problem))
    {
        error = problem;
        return false;
    }

    bool created = false;
    if (!SessionStore::EnsureExists(settings, portName, speed, created, error))
        return false;

    const std::wstring sessionName = SessionStore::NameFor(portName, speed);

    // PuTTY and KiTTY both accept "@session" as a bare argument. The session
    // name is passed unmunged — the terminal applies its own name-to-file
    // mapping.
    std::wstring commandLine = L"\"" + settings.terminalPath + L"\" @" + sessionName;

    // CreateProcessW may write to lpCommandLine, so hand it a mutable buffer.
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    const std::wstring workingDir = Util::GetDirectoryOf(settings.terminalPath);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(settings.terminalPath.c_str(),
                             mutableCmd.data(),
                             nullptr, nullptr, FALSE,
                             0, nullptr,
                             workingDir.empty() ? nullptr : workingDir.c_str(),
                             &si, &pi);
    if (!ok)
    {
        error = L"Could not start the terminal:\n" + settings.terminalPath +
            L"\n\n" + Util::FormatLastError(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace Launcher
