<#
.SYNOPSIS
    Builds TermLaunch and verifies the result is a standalone portable exe.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [switch] $Rebuild,
    [switch] $Clean,
    [switch] $Run
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$solution = Join-Path $root 'termlaunch.sln'

# --- locate MSBuild -------------------------------------------------------

$msbuildCandidates = @(
    'H:\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
)
$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild was not found. Looked in:`n  " + ($msbuildCandidates -join "`n  ")
}

# --- stop a running instance so the linker can write the exe --------------

$running = Get-Process -Name termlaunch -ErrorAction SilentlyContinue
if ($running) {
    Write-Host 'Stopping the running termlaunch.exe so the linker can replace it.'
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 300
}

# --- build ----------------------------------------------------------------

if ($Clean)        { $target = 'Clean' }
elseif ($Rebuild)  { $target = 'Rebuild' }
else               { $target = 'Build' }

Write-Host "$target $Configuration|x64 ..."
& $msbuild $solution "/t:$target" "/p:Configuration=$Configuration" '/p:Platform=x64' '/m' '/v:minimal' '/nologo'
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE." }

if ($Clean) {
    Write-Host 'Clean complete.'
    return
}

$exe = Join-Path $root "x64\$Configuration\termlaunch.exe"
if (-not (Test-Path $exe)) { throw "Build reported success but $exe is missing." }

$size = (Get-Item $exe).Length
Write-Host ("Built {0} ({1:N0} bytes)" -f $exe, $size)

# --- portability check ----------------------------------------------------
#
# The whole point of this project is that copying the exe is enough, so a
# dependency on the VC++ runtime is a build failure, not a warning.

$vcRoot = 'H:\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC'
$dumpbin = $null
if (Test-Path $vcRoot) {
    $toolset = Get-ChildItem $vcRoot | Sort-Object Name -Descending | Select-Object -First 1
    $candidate = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe'
    if (Test-Path $candidate) { $dumpbin = $candidate }
}

if (-not $dumpbin) {
    Write-Warning 'dumpbin.exe not found - skipping the portability check.'
    return
}

$dependents = & $dumpbin /nologo /dependents $exe
$dlls = $dependents |
    Where-Object { $_ -match '^\s{4}\S+\.dll\s*$' } |
    ForEach-Object { $_.Trim() }

$forbidden = $dlls | Where-Object {
    $_ -match '^(VCRUNTIME|MSVCP|MSVCR|CONCRT)' -or
    $_ -match '^api-ms-win-crt' -or
    $_ -match '^ucrtbase'
}

Write-Host ''
Write-Host 'Imports:'
$dlls | ForEach-Object { Write-Host "  $_" }

if ($forbidden) {
    Write-Host ''
    throw ("Not portable - the exe imports the C runtime: " + ($forbidden -join ', ') +
           "`nSet RuntimeLibrary to MultiThreaded (/MT) or MultiThreadedDebug (/MTd) in termlaunch.vcxproj.")
}

Write-Host ''
Write-Host 'Portable: only Windows system DLLs are imported.' -ForegroundColor Green

if ($Run) {
    Write-Host "Starting $exe"
    Start-Process $exe
}
