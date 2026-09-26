---
name: icons
description: Regenerate TermLaunch's tray icons (src/res/*.ico) with tools/make-icons.ps1. Use when the user wants to change the tray icon's look, colours, or the new-port arrival animation frames, or when an icon renders blank or blurry in the notification area.
---

# TermLaunch tray icons

The icons are **generated**, not hand-drawn in an editor, so they can be tuned
by editing numbers in one script. They are checked in so a plain build never
needs PowerShell.

## Regenerate

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\make-icons.ps1
```

Then rebuild — the `.ico` files are linked in as resources, so nothing changes
in the running app until you do.

## What gets produced

| File | Resource | Used for |
| --- | --- | --- |
| `src/res/app.ico` | `IDI_APP` | normal tray state, window class icon, Alt-Tab, exe icon |
| `src/res/pulse1.ico` … `pulse4.ico` | `IDI_PULSE1`…`IDI_PULSE4` | frames of the ~1 s animation when a new COM port arrives |

Each `.ico` contains 16, 20, 24, 32, 48, 64 and 256 px images. 16/20/24 are the
sizes the notification area actually asks for across DPI settings; 256 is for
the file's icon in Explorer.

## Design constraints

- The subject is a **DB-9 serial connector seen face-on**: a D-shaped shell
  (trapezoid with rounded corners, the top edge longer than the bottom) with
  two rows of pins, 5 over 4. That is what makes it read as "serial port" the
  way Device Manager's "Ports (COM & LPT)" class icon does.
- It must be **drawn by us**. Do not extract or ship the icon from
  `setupapi.dll` / `deviceicons.dll` / any Windows binary — those are
  Microsoft's artwork and are not ours to redistribute in a portable exe.
- **At 16 px the pins vanish into mush.** The script switches to a simplified
  form below 20 px: the D-shell plus three larger dots. Check any change at
  16 px before calling it done.
- Keep 1 px of transparent padding at the edges so the tray does not clip it.
- The palette must stay legible on both light and dark taskbars: a mid-tone
  steel-blue body with a darker outline works on both. Pure white or pure black
  silhouettes do not.

## Animation frames

`pulse1`…`pulse4` are the same connector with a green "connected" glow ramping
up and back down. `TrayUI` cycles `app → pulse1 → pulse2 → pulse3 → pulse4 →
pulse3 → pulse2 → pulse1 → app` on a 125 ms timer, which is 8 steps ≈ 1 second,
then restores `IDI_APP`. If you change the frame count, update
`kPulseFrameCount` and `kPulseIntervalMs` in `src/TrayUI.cpp` so the total stays
at about one second.

## How the script builds an .ico

`System.Drawing` can save a `Bitmap` as `.ico` but it flattens to a single
low-colour image, so the script writes the ICO container by hand:

1. Render each size into a 32-bit ARGB `Bitmap` with
   `SmoothingMode = AntiAlias`.
2. For sizes ≤ 64, emit a classic **BMP/DIB** entry: a `BITMAPINFOHEADER` with
   `biHeight` set to *double* the image height (colour mask + AND mask), the
   BGRA rows written **bottom-up**, followed by a padded all-zero AND mask.
3. For 256, emit the **PNG** bytes directly (Vista+ ICO format) to keep the file
   small.
4. Write the `ICONDIR` header and one `ICONDIRENTRY` per image, with `bWidth`
   and `bHeight` set to `0` for 256 px.

Getting `biHeight` or the bottom-up row order wrong is the usual cause of an
icon that renders upside-down, half-transparent, or blank. If an icon looks
broken, check those two things first.

## Verify

```powershell
Get-ChildItem .\src\res\*.ico | Select-Object Name, Length
```

A healthy multi-size icon is roughly 20–120 KB. A few hundred bytes means the
writer produced only one tiny entry and something went wrong.
