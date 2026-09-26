# TermLaunch

A tray icon that lists the serial ports on your machine and opens one in KiTTY
(or PuTTY) when you click it.

![serial connector icon] — the icon is a DB-9 connector, like Device Manager's
"Ports (COM & LPT)" group.

## What it does

- Lists every COM port currently present, with its Device Manager description.
- Clicking a port runs `"kitty_portable.exe" @COM4_1000000` — the saved session
  for that port at the selected speed.
- **Creates the session file if it does not exist**, copying your
  `Default Settings` session (so colours and fonts carry over) and setting
  `SerialLine` and `SerialSpeed` to match.
- **Greys out ports another program already has open.**
- **Pulses the tray icon green for about a second** when a new port appears —
  plug in a USB adapter and you will see it.
- Can start itself on logon.

## Running it

It is a single executable. Copy `termlaunch.exe` anywhere and run it — no
installer, no .NET, no Visual C++ redistributable. It puts an icon in the
notification area and has no window of its own.

Click the icon (either button) for the menu.

## Settings

Right-click the icon → **Settings**.

| Setting | Meaning |
| --- | --- |
| Terminal program | KiTTY or PuTTY executable to run |
| Sessions folder | Where session files live. For KiTTY portable this is the `Sessions` folder next to the exe. |
| Baud rates | The list offered in the menu's **Speed** submenu |
| Default speed | The speed used when you click a port |
| New session defaults | Data bits / stop bits / parity / flow control written into session files this app creates |
| Start automatically on logon | Adds `TermLaunch` under `HKCU\...\CurrentVersion\Run` |
| Animate the icon when a port appears | The ~1 second pulse |
| Grey out ports already in use | See the caveat below |
| Show device names in the menu | `COM4 · Silicon Labs CP210x` vs just `COM4` |

Settings are stored in `termlaunch.ini` **next to the executable**, so the app
stays portable — put it on a USB stick and the configuration travels with it.
If the executable's folder is not writable, it falls back to
`%APPDATA%\TermLaunch\termlaunch.ini`. The **About** box shows which file is in
use.

### Caveat: "grey out ports already in use"

Windows offers no way to ask whether a serial port is open without opening it,
and opening a port asserts DTR/RTS, which **resets some boards** (anything with
Arduino-style auto-reset). TermLaunch therefore only probes when you actually
open the menu, never in the background. If that still bothers your hardware,
turn the option off — ports then always appear enabled.

## Building

Needs Visual Studio 2022 with the C++ desktop workload. From the project folder:

```bash
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
```

The result is `x64\Release\termlaunch.exe`. The script also verifies the exe
imports nothing but Windows system DLLs, which is what keeps it portable.

To redraw the icons after editing `tools/make-icons.ps1`:

```bash
powershell -ExecutionPolicy Bypass -File .\tools\make-icons.ps1
```

## Layout

See [CLAUDE.md](CLAUDE.md) for the source layout, the session-file format, and
the Win32 details worth knowing before changing anything.
