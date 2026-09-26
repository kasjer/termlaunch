#include "SettingsDlg.h"
#include "Util.h"
#include "resource.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{

struct DlgState
{
    Settings *settings = nullptr;
};

const int kStopHalfbits[] = { 2, 3, 4 };                  // 1, 1.5, 2 stop bits
const wchar_t *const kStopLabels[] = { L"1", L"1.5", L"2" };
const wchar_t *const kParityLabels[] = { L"None", L"Odd", L"Even", L"Mark", L"Space" };
const wchar_t *const kFlowLabels[] = { L"None", L"XON/XOFF", L"RTS/CTS", L"DSR/DTR" };

std::wstring GetText(HWND dlg, int id)
{
    HWND ctl = GetDlgItem(dlg, id);
    const int len = GetWindowTextLengthW(ctl);
    if (len <= 0) return std::wstring();
    std::wstring buf((size_t)len + 1, L'\0');
    const int n = GetWindowTextW(ctl, &buf[0], len + 1);
    buf.resize((size_t)(n < 0 ? 0 : n));
    return buf;
}

void SetText(HWND dlg, int id, const std::wstring &text)
{
    SetDlgItemTextW(dlg, id, text.c_str());
}

bool IsChecked(HWND dlg, int id)
{
    return IsDlgButtonChecked(dlg, id) == BST_CHECKED;
}

void SetChecked(HWND dlg, int id, bool on)
{
    CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED);
}

void FillCombo(HWND dlg, int id, const wchar_t *const *labels, int count, int select)
{
    HWND combo = GetDlgItem(dlg, id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)labels[i]);
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)select, 0);
}

int ComboSelection(HWND dlg, int id, int fallback)
{
    LRESULT sel = SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
    return sel == CB_ERR ? fallback : (int)sel;
}

std::wstring BrowseForFile(HWND owner, const std::wstring &current)
{
    wchar_t path[MAX_PATH] = {};
    wcsncpy_s(path, current.c_str(), _TRUNCATE);

    const std::wstring initialDir = Util::GetDirectoryOf(current);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.lpstrTitle = L"Select the terminal program";
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return std::wstring();
    return std::wstring(path);
}

int CALLBACK BrowseCallback(HWND hwnd, UINT msg, LPARAM lParam, LPARAM data)
{
    if (msg == BFFM_INITIALIZED && data)
    {
        // Preselect the folder the user already has configured.
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    }
    UNREFERENCED_PARAMETER(lParam);
    return 0;
}

std::wstring BrowseForFolder(HWND owner, const std::wstring &current)
{
    wchar_t display[MAX_PATH] = {};

    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.pszDisplayName = display;
    bi.lpszTitle = L"Select the sessions folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallback;
    bi.lParam = (LPARAM)current.c_str();

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return std::wstring();

    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? std::wstring(path) : std::wstring();
}

void LoadIntoDialog(HWND dlg, const Settings &s)
{
    SetText(dlg, IDC_TERMINAL_PATH, s.terminalPath);
    SetText(dlg, IDC_SESSIONS_DIR, s.sessionsDir);
    SetText(dlg, IDC_SPEED_LIST, s.SpeedsAsText());

    HWND speedCombo = GetDlgItem(dlg, IDC_DEFAULT_SPEED);
    SendMessageW(speedCombo, CB_RESETCONTENT, 0, 0);
    for (int speed : s.speeds)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", speed);
        SendMessageW(speedCombo, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", s.defaultSpeed);
        SetWindowTextW(speedCombo, buf);
    }

    const wchar_t *const kDataLabels[] = { L"5", L"6", L"7", L"8" };
    FillCombo(dlg, IDC_DATA_BITS, kDataLabels, 4,
              std::max(0, std::min(3, s.dataBits - 5)));

    int stopIndex = 0;
    for (int i = 0; i < 3; ++i)
        if (kStopHalfbits[i] == s.stopHalfbits) stopIndex = i;
    FillCombo(dlg, IDC_STOP_BITS, kStopLabels, 3, stopIndex);

    FillCombo(dlg, IDC_PARITY, kParityLabels, 5,
              std::max(0, std::min(4, s.parity)));
    FillCombo(dlg, IDC_FLOW_CONTROL, kFlowLabels, 4,
              std::max(0, std::min(3, s.flowControl)));

    SetChecked(dlg, IDC_RUN_ON_LOGON, AutoStart::IsEnabled());
    SetChecked(dlg, IDC_DETECT_BUSY, s.detectBusy);
    SetChecked(dlg, IDC_ANIMATE, s.animateOnArrival);
    SetChecked(dlg, IDC_SHOW_FRIENDLY, s.showFriendlyName);

    SetText(dlg, IDC_INI_PATH, L"Settings file:  " + s.iniPath);
}

bool SaveFromDialog(HWND dlg, Settings &s)
{
    Settings edited = s;

    edited.terminalPath = Util::Trim(GetText(dlg, IDC_TERMINAL_PATH));
    edited.sessionsDir = Util::Trim(GetText(dlg, IDC_SESSIONS_DIR));
    edited.SetSpeedsFromText(GetText(dlg, IDC_SPEED_LIST));

    const std::wstring speedText = Util::Trim(GetText(dlg, IDC_DEFAULT_SPEED));
    const int speed = _wtoi(speedText.c_str());
    if (speed < 50)
    {
        Util::ShowError(dlg, L"The default speed must be a number of at least 50 baud.");
        SetFocus(GetDlgItem(dlg, IDC_DEFAULT_SPEED));
        return false;
    }
    edited.defaultSpeed = speed;
    if (std::find(edited.speeds.begin(), edited.speeds.end(), speed) == edited.speeds.end())
    {
        edited.speeds.push_back(speed);
        std::sort(edited.speeds.begin(), edited.speeds.end());
    }

    edited.dataBits = ComboSelection(dlg, IDC_DATA_BITS, 3) + 5;
    edited.stopHalfbits = kStopHalfbits[ComboSelection(dlg, IDC_STOP_BITS, 0)];
    edited.parity = ComboSelection(dlg, IDC_PARITY, 0);
    edited.flowControl = ComboSelection(dlg, IDC_FLOW_CONTROL, 0);

    edited.detectBusy = IsChecked(dlg, IDC_DETECT_BUSY);
    edited.animateOnArrival = IsChecked(dlg, IDC_ANIMATE);
    edited.showFriendlyName = IsChecked(dlg, IDC_SHOW_FRIENDLY);

    if (edited.terminalPath.empty())
    {
        Util::ShowError(dlg, L"Please choose the terminal program to launch.");
        SetFocus(GetDlgItem(dlg, IDC_TERMINAL_PATH));
        return false;
    }
    if (!Util::FileExists(edited.terminalPath))
    {
        const std::wstring msg =
            L"This terminal program was not found:\n\n" + edited.terminalPath +
            L"\n\nSave these settings anyway?";
        if (!Util::AskYesNo(dlg, msg))
        {
            SetFocus(GetDlgItem(dlg, IDC_TERMINAL_PATH));
            return false;
        }
    }
    if (edited.sessionsDir.empty())
    {
        Util::ShowError(dlg, L"Please choose the folder where sessions are stored.");
        SetFocus(GetDlgItem(dlg, IDC_SESSIONS_DIR));
        return false;
    }

    std::wstring error;
    if (!edited.Save(error))
    {
        Util::ShowError(dlg, error);
        return false;
    }

    // Auto-start lives in the registry, not the INI, so apply it separately and
    // report failure without losing the rest of the save.
    const bool wantAutoStart = IsChecked(dlg, IDC_RUN_ON_LOGON);
    if (wantAutoStart != AutoStart::IsEnabled())
    {
        std::wstring autoError;
        if (!AutoStart::SetEnabled(wantAutoStart, autoError))
            Util::ShowError(dlg, autoError);
    }

    s = edited;
    return true;
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DlgState *state = reinterpret_cast<DlgState *>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg)
    {
    case WM_INITDIALOG: {
        state = reinterpret_cast<DlgState *>(lParam);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)state);
        LoadIntoDialog(dlg, *state->settings);

        HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
        if (icon)
        {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_TERMINAL_BROWSE: {
            std::wstring picked = BrowseForFile(dlg, GetText(dlg, IDC_TERMINAL_PATH));
            if (!picked.empty())
            {
                SetText(dlg, IDC_TERMINAL_PATH, picked);
                // A terminal picked from disk usually sits next to its own
                // Sessions folder; offer it when the field is still empty.
                if (Util::Trim(GetText(dlg, IDC_SESSIONS_DIR)).empty())
                {
                    SetText(dlg, IDC_SESSIONS_DIR,
                            Util::PathJoin(Util::GetDirectoryOf(picked), L"Sessions"));
                }
            }
            return TRUE;
        }
        case IDC_SESSIONS_BROWSE: {
            std::wstring picked = BrowseForFolder(dlg, GetText(dlg, IDC_SESSIONS_DIR));
            if (!picked.empty()) SetText(dlg, IDC_SESSIONS_DIR, picked);
            return TRUE;
        }
        case IDOK:
            if (state && SaveFromDialog(dlg, *state->settings))
                EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

} // namespace

namespace SettingsDlg
{

bool Show(HWND owner, HINSTANCE inst, Settings &settings)
{
    DlgState state;
    state.settings = &settings;

    const INT_PTR result = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS),
                                           owner, DialogProc, (LPARAM)&state);
    return result == IDOK;
}

} // namespace SettingsDlg
