<#
.SYNOPSIS
    Generates TermLaunch's tray icons into src/res as multi-resolution .ico files.

.DESCRIPTION
    Draws a DB-9 serial connector with System.Drawing at 16/20/24/32/48/64/256 px
    and packs the results into .ico containers by hand (System.Drawing's own .ico
    writer flattens everything to a single low-colour image).

    Produces app.ico plus pulse1..pulse4.ico, the frames of the ~1 second
    animation played when a new COM port arrives.

    See .claude/skills/icons/SKILL.md for the design constraints.
#>
[CmdletBinding()]
param(
    [string] $OutDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 20, 24, 32, 48, 64, 256)

# ---------------------------------------------------------------- colours ---

function New-Rgb([int]$r, [int]$g, [int]$b) {
    return [System.Drawing.Color]::FromArgb(255, $r, $g, $b)
}

function Blend([System.Drawing.Color]$from, [System.Drawing.Color]$to, [double]$t) {
    if ($t -lt 0) { $t = 0 }
    if ($t -gt 1) { $t = 1 }
    $r = [int][Math]::Round($from.R + ($to.R - $from.R) * $t)
    $g = [int][Math]::Round($from.G + ($to.G - $from.G) * $t)
    $b = [int][Math]::Round($from.B + ($to.B - $from.B) * $t)
    return (New-Rgb $r $g $b)
}

$OutlineBase = New-Rgb 0x1B 0x36 0x57   # dark navy
$BodyHiBase  = New-Rgb 0x5B 0x93 0xC9   # steel blue, lit edge
$BodyLoBase  = New-Rgb 0x2C 0x58 0x85   # steel blue, shadow edge
$PinBase     = New-Rgb 0xE4 0xEE 0xF8   # near-white contacts

$OutlineGlow = New-Rgb 0x14 0x54 0x25
$BodyHiGlow  = New-Rgb 0x56 0xCC 0x70
$BodyLoGlow  = New-Rgb 0x1E 0x8A 0x3C
$PinGlow     = New-Rgb 0xFF 0xFF 0xFF

# ---------------------------------------------------------------- drawing ---

function Add-Dot {
    param([System.Drawing.Graphics]$G, [System.Drawing.Brush]$Brush,
          [double]$Cx, [double]$Cy, [double]$R)
    $G.FillEllipse($Brush, [float]($Cx - $R), [float]($Cy - $R),
                           [float]($R * 2), [float]($R * 2))
}

function Draw-Connector {
    param(
        [System.Drawing.Graphics] $G,
        [int]    $Size,
        [double] $Glow   # 0 = idle, 1 = fully lit
    )

    $outline = Blend $OutlineBase $OutlineGlow $Glow
    $bodyHi  = Blend $BodyHiBase  $BodyHiGlow  $Glow
    $bodyLo  = Blend $BodyLoBase  $BodyLoGlow  $Glow
    $pin     = Blend $PinBase     $PinGlow     $Glow

    $s = [double]$Size
    # Stroke width; the pen's round joins are what rounds the shell's corners.
    $pw = [Math]::Max(1.0, $s * 0.085)

    # Jack screws either side of the shell. Below 24 px they collapse into
    # indistinct blobs, so they are dropped and the shell widened instead.
    $wideShell = $Size -lt 24
    if ($wideShell) { $xl = 0.13; $xr = 0.87 } else { $xl = 0.20; $xr = 0.80 }
    $inset = 0.075

    $yTop = 0.245
    $yBot = 0.775

    $pts = @(
        (New-Object System.Drawing.PointF([float]($xl * $s),            [float]($yTop * $s))),
        (New-Object System.Drawing.PointF([float]($xr * $s),            [float]($yTop * $s))),
        (New-Object System.Drawing.PointF([float](($xr - $inset) * $s), [float]($yBot * $s))),
        (New-Object System.Drawing.PointF([float](($xl + $inset) * $s), [float]($yBot * $s)))
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddPolygon($pts)
    $path.CloseFigure()

    $gradRect = New-Object System.Drawing.RectangleF(
        [float]0, [float]($yTop * $s - 1), [float]$s, [float](($yBot - $yTop) * $s + 2))
    $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $gradRect, $bodyHi, $bodyLo,
        [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)

    $pen = New-Object System.Drawing.Pen($outline, [float]$pw)
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round

    # Jack screws first, so the shell's outline overlaps them cleanly.
    if (-not $wideShell) {
        $screwR = $s * 0.075
        $screwY = ($yTop + $yBot) / 2 * $s
        $screwBrush = New-Object System.Drawing.SolidBrush($bodyLo)
        $screwPen = New-Object System.Drawing.Pen($outline, [float]($pw * 0.8))
        foreach ($cx in @(0.095, 0.905)) {
            Add-Dot $G $screwBrush ($cx * $s) $screwY $screwR
            $G.DrawEllipse($screwPen,
                [float]($cx * $s - $screwR), [float]($screwY - $screwR),
                [float]($screwR * 2), [float]($screwR * 2))
        }
        $screwBrush.Dispose(); $screwPen.Dispose()
    }

    $G.DrawPath($pen, $path)
    $G.FillPath($fill, $path)

    # Contacts: 5 over 4 at usable sizes, 3 large dots when detail would mush.
    $pinBrush = New-Object System.Drawing.SolidBrush($pin)
    if ($Size -lt 20) {
        $r = $s * 0.088
        foreach ($cx in @(0.32, 0.50, 0.68)) {
            Add-Dot $G $pinBrush ($cx * $s) ($s * 0.51) $r
        }
    }
    else {
        $r = $s * 0.050
        $x0 = $xl + 0.075; $x1 = $xr - 0.075
        for ($i = 0; $i -lt 5; $i++) {
            $cx = $x0 + ($x1 - $x0) * $i / 4.0
            Add-Dot $G $pinBrush ($cx * $s) ($s * 0.415) $r
        }
        $x0b = $x0 + ($x1 - $x0) * 0.125; $x1b = $x1 - ($x1 - $x0) * 0.125
        for ($i = 0; $i -lt 4; $i++) {
            $cx = $x0b + ($x1b - $x0b) * $i / 3.0
            Add-Dot $G $pinBrush ($cx * $s) ($s * 0.600) $r
        }
    }

    $pinBrush.Dispose(); $pen.Dispose(); $fill.Dispose(); $path.Dispose()
}

function New-IconBitmap {
    param([int]$Size, [double]$Glow)

    $bmp = New-Object System.Drawing.Bitmap($Size, $Size,
              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    Draw-Connector -G $g -Size $Size -Glow $Glow

    $g.Dispose()
    return $bmp
}

# ------------------------------------------------------------- ico writer ---

function Get-DibBytes {
    param([System.Drawing.Bitmap]$Bmp)

    $w = $Bmp.Width; $h = $Bmp.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $Bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $data.Stride
        $raw = New-Object byte[] ($stride * $h)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $raw, 0, $raw.Length)
    }
    finally {
        $Bmp.UnlockBits($data)
    }

    $rowBytes  = $w * 4
    $maskStride = [int]([Math]::Floor(($w + 31) / 32) * 4)

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER. biHeight is doubled: colour bitmap + AND mask.
    $bw.Write([uint32]40)
    $bw.Write([int32]$w)
    $bw.Write([int32]($h * 2))
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]0)                                   # BI_RGB
    $bw.Write([uint32]($rowBytes * $h + $maskStride * $h))  # biSizeImage
    $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)

    # Colour rows, bottom-up.
    for ($y = $h - 1; $y -ge 0; $y--) {
        $bw.Write($raw, $y * $stride, $rowBytes)
    }

    # AND mask: unused for 32-bit icons, but must be present and padded.
    $zeroRow = New-Object byte[] $maskStride
    for ($y = 0; $y -lt $h; $y++) {
        $bw.Write($zeroRow, 0, $maskStride)
    }

    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose(); $ms.Dispose()
    return ,$bytes
}

function Get-PngBytes {
    param([System.Drawing.Bitmap]$Bmp)
    $ms = New-Object System.IO.MemoryStream
    $Bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return ,$bytes
}

function Write-IcoFile {
    param([string]$Path, [double]$Glow)

    $images = New-Object System.Collections.ArrayList
    foreach ($size in $sizes) {
        $bmp = New-IconBitmap -Size $size -Glow $Glow
        if ($size -ge 256) { $bytes = Get-PngBytes -Bmp $bmp }
        else               { $bytes = Get-DibBytes -Bmp $bmp }
        [void]$images.Add([pscustomobject]@{ Size = $size; Bytes = $bytes })
        $bmp.Dispose()
    }

    $fs = [System.IO.File]::Create($Path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    try {
        $bw.Write([uint16]0)                  # reserved
        $bw.Write([uint16]1)                  # type: icon
        $bw.Write([uint16]$images.Count)

        $offset = 6 + 16 * $images.Count
        foreach ($img in $images) {
            if ($img.Size -ge 256) { $dim = 0 } else { $dim = $img.Size }
            $bw.Write([byte]$dim)             # width  (0 means 256)
            $bw.Write([byte]$dim)             # height (0 means 256)
            $bw.Write([byte]0)                # palette entries
            $bw.Write([byte]0)                # reserved
            $bw.Write([uint16]1)              # colour planes
            $bw.Write([uint16]32)             # bits per pixel
            $bw.Write([uint32]$img.Bytes.Length)
            $bw.Write([uint32]$offset)
            $offset += $img.Bytes.Length
        }
        foreach ($img in $images) {
            $bw.Write($img.Bytes, 0, $img.Bytes.Length)
        }
    }
    finally {
        $bw.Dispose(); $fs.Dispose()
    }
}

# ------------------------------------------------------------------- main ---

if ([string]::IsNullOrEmpty($OutDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
    $OutDir = Join-Path $scriptDir '..\src\res'
}
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
$OutDir = (Resolve-Path $OutDir).Path

$targets = @(
    @{ Name = 'app.ico';    Glow = 0.00 },
    @{ Name = 'pulse1.ico'; Glow = 0.25 },
    @{ Name = 'pulse2.ico'; Glow = 0.50 },
    @{ Name = 'pulse3.ico'; Glow = 0.75 },
    @{ Name = 'pulse4.ico'; Glow = 1.00 }
)

foreach ($t in $targets) {
    $path = Join-Path $OutDir $t.Name
    Write-IcoFile -Path $path -Glow ([double]$t.Glow)
    $len = (Get-Item $path).Length
    Write-Host ("  {0,-12} {1,7:N0} bytes  ({2} images)" -f $t.Name, $len, $sizes.Count)
}

Write-Host "Icons written to $OutDir"
