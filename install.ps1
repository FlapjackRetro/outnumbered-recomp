<#
  OutNumbered! static recompilation - Windows installer / builder.
  Usage:   powershell -ExecutionPolicy Bypass -File install.ps1 [-GameDir C:\DOS\Outnumbered] [-Msys C:\msys64]
  Needs the ORIGINAL game files (SSO.EXE plus the .GRA/.DAT files). Nothing from the game is uploaded or
  redistributed: the C code is generated on this PC from your own SSO.EXE.
  Installs (if missing): MSYS2 + clang/cmake/ninja/SDL2/python/git, and fetches the pcrecomp toolbox.
  Result: build\sso.exe and play.bat next to this script.
#>
param(
  [string]$GameDir = "",
  [string]$Msys = "C:\msys64"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# 1. game files: -GameDir, else this folder's parent (recomp\ lives inside the game folder), else this folder
if (-not $GameDir) {
  foreach ($c in @((Split-Path -Parent $root), $root, (Get-Location).Path)) { if (Test-Path (Join-Path $c "SSO.EXE")) { $GameDir = $c; break } }
}
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir "SSO.EXE"))) {
  throw "SSO.EXE not found. Re-run with -GameDir <folder containing SSO.EXE and the .GRA/.DAT files>."
}
$GameDir = (Resolve-Path $GameDir).Path
if (-not (Test-Path (Join-Path $GameDir "MOCKET.DAT"))) { Write-Warning "MOCKET.DAT not found in $GameDir - the game needs its data files to run." }
Write-Host "Game files: $GameDir"

# 2. MSYS2
$bash = Join-Path $Msys "usr\bin\bash.exe"
if (-not (Test-Path $bash)) {
  Write-Host "MSYS2 not found at $Msys - installing with winget..."
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw "winget is unavailable. Install MSYS2 from https://www.msys2.org to $Msys and re-run." }
  winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements --location $Msys
  if (-not (Test-Path $bash)) { throw "MSYS2 install did not produce $bash. Install it manually from https://www.msys2.org and re-run." }
}
$env:MSYSTEM = "MINGW64"; $env:CHERE_INVOKING = "1"
function Msh([string]$cmd) {
  & $bash -lc $cmd
  if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $cmd" }
}
function Unix([string]$p) { (& $bash -lc "cygpath -u '$p'").Trim() }

# 3. packages
Write-Host "Installing build tools (pacman)..."
Msh "pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python git"

# 4. lift + build (tools/build.py fetches pcrecomp into .pcrecomp\ on first run)
Write-Host "Lifting and building (a few minutes)..."
$r = Unix $root; $d = Unix $GameDir
Msh "cd '$r' && python tools/build.py --game '$d'"

# 5. launcher
$play = Join-Path $root "play.bat"
@"
@echo off
rem Launches the recompiled OutNumbered!. SSO_AUDIO=speaker forces the PC speaker.
set PATH=$Msys\mingw64\bin;%PATH%
"%~dp0build\sso.exe" "$GameDir" %*
"@ | Set-Content -Encoding ASCII $play
Write-Host ""
Write-Host "Done. Run play.bat to start the game." -ForegroundColor Green
