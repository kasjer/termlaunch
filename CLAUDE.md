# TermLaunch

Windows system-tray launcher for serial terminals. It enumerates the COM ports
currently present on the machine, shows them in a tray menu, and launches
KiTTY (or PuTTY) against a saved session for the selected port + baud rate,
creating the session file on the fly when it does not exist yet.

## Hard requirements (from the user — do not regress these)

1. **Portable single executable.** No installer, no redistributable, no .NET,
   no VC++ runtime DLLs. Copying `termlaunch.exe` to another machine must be
   enough. Enforced by: Win32 C++ with `/MT` (static CRT) and no third-party
   libraries.
2. **x64 only**, targeting Windows 10 and Windows 11 on Intel/AMD.
3. Tray menu lists every COM port currently present.
4. Selecting a port launches the terminal as
   `"<terminal>" @<Port>_<Speed>` — e.g. `"kitty_portable.exe" @COM4_1000000`.
5. If the session does not exist, create it, with `SerialLine` and
   `SerialSpeed` matching the session name.
5b. **Both of KiTTY's storage backends.** KiTTY keeps sessions either as files
   or in the registry, chosen by `savemode` in kitty.ini. Support both, pick
   automatically, and let the user pin it.
6. A port that is present but **already open by another process is greyed out**.
7. When a **new** COM port appears, the tray icon **animates for ~1 second**.
8. The icon reads as a serial port (DB-9 connector), like Device Manager's
   "Ports (COM & LPT)" class icon — but drawn by us, not copied from Windows.
9. Settings include **start automatically on logon**.

## Layout

```
src/
  main.cpp             WinMain, window proc, message loop, single-instance mutex
  PortEnum.*           SetupAPI enumeration of present COM ports + busy probing
  KittyConfig.*        finds kitty.ini and reads savemode/configdir/fileextension
  SessionStore.*       creates sessions, in files or in the registry
  Settings.*           portable INI load/save, run-on-logon registry toggle
  Launcher.*           builds the command line and CreateProcess
  TrayUI.*             Shell_NotifyIcon, menu building, arrival animation
  SettingsDlg.*        settings dialog
  Util.*               string/path/registry helpers, RAII wrappers
  resource.h           resource IDs
  termlaunch.rc        icons, dialog, version info
  termlaunch.manifest  DPI-aware + comctl32 v6 + requestedExecutionLevel
  res/                 generated .ico files (checked in)
tools/make-icons.ps1   draws res/*.ico with System.Drawing
build.ps1              wrapper around msbuild + portability verification
```

## Build

Visual Studio 2022 Community lives at `H:\Microsoft Visual Studio\2022\Community`.
There is a `build` skill with the exact commands; the short version:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
```

Output: `x64\Release\termlaunch.exe`. Toolset v143, Windows SDK 10.0.22621.0.

**Always verify portability after a release build** — `dumpbin /dependents`
must list only Windows system DLLs (no `VCRUNTIME*`, no `MSVCP*`, no
`api-ms-win-crt-*`). `build.ps1` does this automatically and fails the build if
a runtime dependency creeps in.

## Environment on this machine (defaults baked into Settings)

| Setting | Default |
| --- | --- |
| Terminal | `C:\Users\Jerzy\Downloads\kitty_portable.exe` |
| Sessions folder | `C:\Users\Jerzy\Downloads\Sessions` |
| Default speed | `1000000` |

The sessions folder is KiTTY-portable's own `Sessions` folder (it sits next to
`kitty_portable.exe`), which is why `@COM4_1000000` resolves.

## Where sessions live

KiTTY stores sessions in one of two places and decides by the `savemode` line
in `kitty.ini`. `KittyConfig::Detect` looks for that file **beside the terminal
executable first** — that is portable mode, and it wins — then in
`%APPDATA%\KiTTY\kitty.ini`.

| `savemode` | Backend |
| --- | --- |
| `dir` | files in a Sessions folder |
| `registry` | `HKCU\SOFTWARE\9bis.com\KiTTY\Sessions` |
| absent, or no kitty.ini at all | **registry** — that is KiTTY's default |
| `file` | one flat file; KiTTY calls it unmaintained and we refuse it with a clear message |

`SaveMode=auto\|dir\|registry` in termlaunch.ini pins the choice; `auto` is the
default and means "read kitty.ini". `RegistryPath` overrides the key, which is
how PuTTY gets supported (`SOFTWARE\SimonTatham\PuTTY\Sessions`).

Both machines' ini files are worth remembering as test cases: the one in
`Downloads` next to `kitty_portable.exe` sets `savemode=dir`, while the one in
`%APPDATA%\KiTTY` has every savemode line commented out — so the same app
resolves to different backends depending on which terminal it is pointed at.

## Session file format

PuTTY/KiTTY portable session files are **ASCII, LF line endings**, one
`Key\value\` pair per line, no header. Values containing reserved characters
are URL-encoded (`Default%20Settings` is the session literally named
"Default Settings"; `Courier%20New` is a font name). Relevant keys:

```
Protocol\serial\
SerialLine\COM4\
SerialSpeed\1000000\
SerialDataBits\8\
SerialStopHalfbits\2\      (2 half-bits = 1 stop bit)
SerialParity\0\            (0 = none)
SerialFlowControl\0\       (0 = none, 1 = XON/XOFF, 2 = RTS/CTS)
```

New session files are built from `Default%20Settings` in the sessions folder
when it exists (so the user's colours/fonts carry over), otherwise from the
template embedded in `SessionStore.cpp`. Either way the keys above are
overwritten to match the file name, and `Protocol` is forced to `serial`.
Writing must preserve LF endings and key order, and must not add a BOM.

## Session registry format

Same settings, same munged names, different shapes. One subkey per session
under the Sessions key, named with `Util::MungeSessionName` exactly as the file
name is — the default session really is stored as `Default%20Settings`.

`kitty_sessions.reg` in the repo root is a real export of that key (checked for
credentials first — every password field in it is empty) and is the reference
for what the shapes actually look like.

**Types matter**: each setting is `REG_SZ` or `REG_DWORD` and the terminal
cares which. From that export, `SerialLine` is `REG_SZ "COM3"` while
`SerialSpeed` is `REG_DWORD 0x000f4240` — the same value that a session *file*
writes as the text `SerialSpeed\1000000\`. `SerialDataBits`,
`SerialStopHalfbits`, `SerialParity` and `SerialFlowControl` are all DWORDs;
`Protocol` is a string.

**Values are encoded differently in the two backends.** A session *file*
percent-encodes its values with the same mungestr the file names use; the
registry stores them decoded. The same setting therefore looks like this:

| Setting | Registry | File |
| --- | --- | --- |
| `WinTitle` | `%%s` | `WinTitle\%25%25s\` |
| `Font` | `Courier New` | `Font\Courier%20New\` |
| `ProxyTelnetCommand` | `connect %host %port\n` | `connect%20%25host%20%25port%5Cn` |

So `Override` values in `SessionStore.cpp` are held **decoded**, and
`EncodeForFile` munges them on the way into a file while the registry backend
writes them raw. This is invisible for `COM4`, `serial` and digits — which is
exactly why it is easy to get wrong and only notice on a value like `WinTitle`.

That is why `kFallbackTemplate` in `SessionStore.cpp` is a typed table rather
than a block of text: the directory backend renders a DWORD as decimal text,
the registry backend writes it as a DWORD, and neither can drift from the
other. When inheriting from an existing `Default Settings` key, values are
copied with `RegEnumValueW` and written back **with their original type** —
never re-typed.

## Gotchas worth remembering

- **Probing whether a port is busy opens it.** `CreateFile("\\\\.\\COM4", ...)`
  returning `ERROR_ACCESS_DENIED` is the only reliable "in use" signal, but the
  serial driver's open path can assert DTR/RTS and reset attached boards
  (Arduino-style auto-reset). So the probe runs **only while building the menu**
  (a user click), never on a timer, it opens with `dwDesiredAccess = 0`
  (query-only, no read/write), and `DetectBusy=0` in the INI disables it.
- `\\.\COMx` (the `\\.\` prefix) is required for COM10 and above.
- **kitty.ini files use two different comment characters.** The one KiTTY ships
  into `%APPDATA%` comments with `;`, the one written beside a portable exe
  uses `#`. Treat both as comments, or a switched-off `;savemode=registry` line
  reads as if it were live.
- A `//` comment must never end with a backslash — it continues onto the next
  line and silently swallows it. This actually bit the `SaveMode` enum, where
  a comment ending `...under Sessions\` ate the `Registry` enumerator. Watch
  for it whenever a comment ends in a Windows path or a `Key\value\` example.
- A registry session key that exists but holds **no values** is a leftover, not
  a session; treat it as absent and fill it in.
- Enumerate with `GUID_DEVCLASS_PORTS` + `DIGCF_PRESENT`, then read `PortName`
  from the device's software key. That class also contains LPT ports — filter on
  `PortName` starting with `COM`. `SPDRP_FRIENDLYNAME` gives the Device Manager
  string; fall back to `SPDRP_DEVICEDESC`.
- Port *numbers* must sort numerically: COM9 before COM10, not after.
- `RegisterDeviceNotification` with `GUID_DEVINTERFACE_COMPORT` catches USB
  adapters. Some virtual/legacy ports only surface via `DBT_DEVNODES_CHANGED`,
  so handle both and debounce with a short timer before rescanning.
- Tray icons must be re-added when Explorer restarts — register and handle the
  `TaskbarCreated` message.
- Menu item text with `&` must be escaped as `&&`, and friendly names from
  drivers can legitimately contain `&`.
- A tray context menu needs `SetForegroundWindow` before `TrackPopupMenu` and a
  `PostMessage(WM_NULL)` after, or the menu will not dismiss on click-away.
- The INI lives next to the exe (portable). If that directory is not writable
  (read-only USB stick, Program Files), fall back to
  `%APPDATA%\TermLaunch\termlaunch.ini`. Never hard-require the exe directory.
- Run-on-logon is `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value
  `TermLaunch`, data `"<full exe path>"` quoted. It must be rewritten if the
  exe moves; the app refreshes it at startup when the value already exists.

## Code style

Formatting is defined by `.clang-format` in the repo root. Match it when
writing or editing code here.

```cpp
namespace Util
{

const wchar_t *const kAppTitle = L"TermLaunch";

bool StartsWithI(const std::wstring &s, const std::wstring &prefix)
{
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), (int)prefix.size(),
                                prefix.c_str(), (int)prefix.size(), TRUE) == CSTR_EQUAL;
}

} // namespace Util
```

- **Allman braces.** Opening brace on its own line for namespaces, functions,
  structs, enums, `if`/`else`/`for`/`while`/`switch`. `else` starts a new line.
- **`*` and `&` bind to the name**: `const std::wstring &s`, `wchar_t *buf`,
  `const wchar_t *const kName`.
- 4 spaces, never tabs. Namespace bodies are **not** indented. `case` labels are
  **not** indented relative to `switch`. `public:`/`private:` outdent by 4.
- **Lines are wrapped by hand and the formatter preserves those breaks**
  (`ColumnLimit: 0`). Keep to a soft ~95 columns; the widest line in the tree
  is 93.
- Continuation of a broken expression indents **+4**, it does not align under
  the first operand. But arguments that begin on the same line as `(` **do**
  align under that paren (see `CompareStringOrdinal` above).
- **Short bodies stay on one line** when the author wrote them that way:
  `if (s.empty()) return false;`, `if (icon) { DestroyIcon(icon); icon = nullptr; }`,
  `[&](const SerialPort &p) { return Util::IEquals(p.name, portName); }`.
- Braced lists carry inner spaces: `{ 1, 2, 3 }`, not `{1, 2, 3}`.
- Includes are grouped by hand — own header, Windows headers, C++ stdlib,
  project headers — so `SortIncludes` is off. Don't reorder them.
- `.cpp` files are divided by banner comments:
  `// ---- strings -------------------------------------------------------------`

### Three things `.clang-format` cannot express

Running clang-format over the tree reproduces about 92% of it. The remaining
differences are all cases where the formatter would *undo* deliberate hand
formatting, so **do not do a wholesale reformat** — format only the code you
touch, and re-check these by eye:

1. **Aligned declaration columns** — `HWND      hwnd = nullptr;` in `struct App`,
   `const int  kPulseSequence[]` in `TrayUI.cpp`. clang-format collapses these
   to single spaces. (`AlignConsecutiveDeclarations` does not reproduce them
   either; it makes things worse.)
2. **Trailing comments indented for readability** — `bool canDot = false;   // ...`
   uses three spaces; clang-format forces one.
3. **One-line brace blocks** — `if (n < buf.size()) { buf.resize(n); return buf; }`
   stays on one line here, but Allman mode overrides
   `AllowShortBlocksOnASingleLine` and expands it to four lines.

### Conversion state

The whole tree is converted — all 8 `.cpp` files and all 8 headers. New code
should match without exception. `resource.h` is only `#define`s and has no
formatting to speak of.

## Conventions

- C++17, Unicode only (`UNICODE`/`_UNICODE`), `std::wstring` everywhere;
  no `TCHAR`, no ANSI API calls.
- No exceptions across the Win32 boundary; functions return `bool` and report
  user-facing failures through `Util::ShowError`.
- RAII wrappers for `HANDLE`/`HKEY`/`HMENU` live in `Util.h` — use them rather
  than manual cleanup.
- Resource IDs: `IDI_*` icons, `IDD_*` dialogs, `IDC_*` controls,
  `IDM_*` static menu commands. Dynamic per-port menu commands start at
  `IDM_PORT_FIRST` (40000) and are index-based; dynamic speed commands start at
  `IDM_SPEED_FIRST` (41000). Keep those ranges clear of static IDs.

## Working in this repo

- `cat`-style heredocs through the Bash tool have hung in this environment —
  use the Write tool for file content and PowerShell for directory work.
- The project path contains spaces (`H:\work\Visual Studio 2022\termlaunch`);
  quote it in every command line.
