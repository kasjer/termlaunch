---
name: build
description: Build, clean, verify and run TermLaunch with MSBuild from Visual Studio 2022 Community on H:. Use whenever the user asks to build, rebuild, compile, clean, run, or check that termlaunch.exe is still a dependency-free portable executable.
---

# Building TermLaunch

## Toolchain on this machine

| Thing | Path / value |
| --- | --- |
| VS 2022 Community | `H:\Microsoft Visual Studio\2022\Community` |
| MSBuild | `H:\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe` |
| MSVC toolset | v143 (14.41.34120) |
| Windows SDK | 10.0.22621.0 |
| Platform | x64 only — there is no Win32/ARM64 configuration |

## Normal build

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
```

`build.ps1` wraps MSBuild, then runs the portability check below and fails if it
does not pass. Use `-Configuration Debug` for a debug build, `-Rebuild` to force
a full rebuild, `-Clean` to clean only.

## Raw MSBuild (when you need to see MSBuild's own diagnostics)

Quote the paths — both the VS path and the project path contain spaces.

```powershell
& "H:\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
    ".\termlaunch.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

Raise verbosity with `/v:normal` or `/v:detailed` when a build fails for a
reason the minimal log does not explain. Add `/t:Rebuild` to force, `/t:Clean`
to clean.

Output lands in `x64\Release\termlaunch.exe` (or `x64\Debug\`).

## Portability verification — run after every Release build

This is a hard requirement of the project: the exe must run on a clean Windows
10/11 machine with nothing installed.

```powershell
$vc = Get-ChildItem "H:\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" |
      Sort-Object Name -Descending | Select-Object -First 1
& "$($vc.FullName)\bin\Hostx64\x64\dumpbin.exe" /dependents ".\x64\Release\termlaunch.exe"
```

The import list must contain **only** Windows system DLLs — expect roughly
`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `SHELL32.dll`,
`ole32.dll`, `SETUPAPI.dll`, `COMCTL32.dll`, `COMDLG32.dll`, `SHLWAPI.dll`.

It must **not** contain any of:

- `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`
- `MSVCP140.dll`
- `api-ms-win-crt-*.dll`
- `ucrtbase.dll`

If any of those appear, the static-CRT setting was lost. Fix it in
`termlaunch.vcxproj`: `<RuntimeLibrary>MultiThreaded</RuntimeLibrary>` for
Release and `MultiThreadedDebug` for Debug (i.e. `/MT` and `/MTd`, never the
`*DLL` variants). Also check that no new `.cpp` pulled in a library that forces
a dynamic CRT.

## Running it

TermLaunch has no main window — it only puts an icon in the notification area.

```powershell
Start-Process ".\x64\Release\termlaunch.exe"
```

It is single-instance (named mutex `TermLaunch.SingleInstance.v1`); launching a
second copy just surfaces the existing one's menu and exits. To stop it:

```powershell
Stop-Process -Name termlaunch -ErrorAction SilentlyContinue
```

**Always stop a running instance before rebuilding** — otherwise the linker
fails with `LNK1168: cannot open ... termlaunch.exe for writing`. `build.ps1`
does this for you.

## Common failures

| Symptom | Cause / fix |
| --- | --- |
| `LNK1168: cannot open termlaunch.exe for writing` | An instance is running. `Stop-Process -Name termlaunch`. |
| `MSB8020: The build tools for v143 cannot be found` | Wrong `PlatformToolset`, or the C++ workload is not installed. Check `VC\Tools\MSVC` has a `14.4x` folder. |
| `MSB8036: The Windows SDK version 10.0.x was not found` | Set `<WindowsTargetPlatformVersion>` in the vcxproj to a version present under `C:\Program Files (x86)\Windows Kits\10\Include`. 10.0.22621.0 is installed here. |
| `RC1015: cannot open include file 'winres.h'` | The `.rc` should include `<windows.h>`, not `winres.h`, and the resource compiler needs the SDK include path — normally supplied by the vcxproj. |
| Icon looks wrong or blank in the tray | Regenerate `src/res/*.ico` with the `icons` skill, then rebuild. |
