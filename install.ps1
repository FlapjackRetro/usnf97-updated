<#
.SYNOPSIS
  Installs Jane's USNF '97 from the original CD on Windows 11 without EA's setup program.

.DESCRIPTION
  EA's SETUP.EXE needs CTL3D32.DLL (absent on Windows 11) and tries to install the
  DirectX 3 runtime and drivers. This script does what its install script (SETUP.SSF)
  did, minus the DirectX/driver/uninstaller parts:
    1. unpacks SETUP.ESA (USNF.EXE, USNF.SMS, USNF_1/2.LIB, Miles + CommDrv DLLs, docs)
    2. copies the CD-only data (USNF_3/7/8/10.LIB) so the disc is not needed
    3. runs the official 1.4 patch (usnf14.exe) against the new install
    4. deploys usnf97fix (ddraw.dll + usnf97fix.ini + usnf97cfg.exe, the controls configurator)
    5. creates USNF97.EXE, a renamed copy of USNF.EXE, and points the shortcut at it.
       Windows' compatibility database applies "IgnoreAltTab" (swallows Alt+Tab and the
       Windows key) to any exe named USNF.EXE; usnf97fix does the job of the other fix in
       that entry ("USNF97": CLI/STI and division overflow), so nothing is lost.

.PARAMETER Disc
  Drive or folder with the USNF '97 CD contents (SETUP.ESA, USNF_10.LIB, ...). Default H:\

.PARAMETER GameDir
  Where to install. Default C:\JANES\USNF97 (the original default, and where the 1.4
  patch looks first). Keep it short: the game stores this path in an 80-byte buffer.

.PARAMETER Patch
  Path to usnf14.exe (the official 1.4 update). Default: next to this script.

.PARAMETER SkipPatch
  Install the 1.0 files only (not recommended; 1.4 fixes many game bugs).

.PARAMETER KeepDataOnCD
  Don't copy the ~640 MB of CD data; the game will then need the disc (or a mounted image).
#>
param(
    [string]$Disc = 'H:\',
    [string]$GameDir = 'C:\JANES\USNF97',
    [string]$Patch = (Join-Path $PSScriptRoot 'usnf14.exe'),
    [switch]$SkipPatch,
    [switch]$KeepDataOnCD
)
$ErrorActionPreference = 'Stop'

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# ---------- checks ----------
$esa = Join-Path $Disc 'SETUP.ESA'
if (-not (Test-Path $esa)) { throw "SETUP.ESA not found in '$Disc'. Point -Disc at the USNF '97 CD." }
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw "Python 3 is needed for the archive extractor ('python' not on PATH)." }
# newest of dist\ and build\ (a stale dist\ copy must not win over a fresh build)
$dll = @('dist\ddraw.dll', 'build\ddraw.dll') | ForEach-Object { Join-Path $PSScriptRoot $_ } |
    Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTime } -Descending | Select-Object -First 1
if (-not $dll) { throw "ddraw.dll not found in dist\ or build\ (build it with build.sh first)." }
if ((Join-Path $GameDir '').Length -gt 60) {
    Write-Warning "'$GameDir' is long; the game can only hold a 60-character data path. The fix will try the 8.3 short name."
}
if (Test-Path (Join-Path $GameDir 'USNF.EXE')) {
    throw "USNF.EXE already exists in '$GameDir'. Remove it or choose another -GameDir."
}

# ---------- 1. unpack SETUP.ESA ----------
Step "Unpacking SETUP.ESA to $GameDir"
New-Item -ItemType Directory -Force -Path $GameDir | Out-Null
& python -I (Join-Path $PSScriptRoot 'tools\esa_extract.py') $esa $GameDir
if ($LASTEXITCODE -ne 0) { throw "Extraction failed." }
# EA's uninstaller and the installer's own decompressor are not needed.
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $GameDir 'EAREMOVE.EXE'), (Join-Path $GameDir 'PKCOMP.IDKDECODLL')

# ---------- 2. CD-only data ----------
if (-not $KeepDataOnCD) {
    Step "Copying CD data files (about 640 MB)"
    foreach ($lib in Get-ChildItem -Path $Disc -Filter 'USNF_*.LIB') {
        Write-Host "    $($lib.Name)"
        Copy-Item $lib.FullName -Destination $GameDir -Force
    }
}
# Files copied off a CD are read-only; the patch and the game need to write.
Get-ChildItem $GameDir -File | ForEach-Object { $_.IsReadOnly = $false }

# ---------- 3. 1.4 patch ----------
if (-not $SkipPatch) {
    if (-not (Test-Path $Patch)) { throw "Patch '$Patch' not found (use -Patch or -SkipPatch)." }
    $before = (Get-FileHash (Join-Path $GameDir 'USNF.EXE')).Hash
    Step "Running the official 1.4 patch"
    Write-Host "    The patcher's window will ask to confirm; if it asks for the game folder, choose $GameDir"
    Start-Process -FilePath $Patch -WorkingDirectory $GameDir -Wait
    $after = (Get-FileHash (Join-Path $GameDir 'USNF.EXE')).Hash
    if ($before -eq $after) {
        Write-Warning "USNF.EXE did not change: the patch was cancelled or failed. Re-run usnf14.exe manually against $GameDir."
    } else {
        Write-Host "    USNF.EXE updated to 1.4"
    }
}

# ---------- 4. usnf97fix ----------
Step "Installing usnf97fix (ddraw.dll)"
Copy-Item $dll -Destination $GameDir -Force
$ini = Join-Path $GameDir 'usnf97fix.ini'
if (-not (Test-Path $ini)) { Copy-Item (Join-Path $PSScriptRoot 'usnf97fix.ini') -Destination $ini }
# the controls configurator (newest of dist\ and build\, like the DLL)
$cfg = @('dist\usnf97cfg.exe', 'build\usnf97cfg.exe') | ForEach-Object { Join-Path $PSScriptRoot $_ } |
    Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTime } -Descending | Select-Object -First 1
if ($cfg) { Copy-Item $cfg -Destination $GameDir -Force }

# ---------- 5. USNF97.EXE ----------
# The game finds its symbol map as <exe name>.SMS, so the copy needs one too.
Step "Creating USNF97.EXE (avoids Windows' IgnoreAltTab compatibility fix)"
Copy-Item (Join-Path $GameDir 'USNF.EXE') (Join-Path $GameDir 'USNF97.EXE') -Force
Copy-Item (Join-Path $GameDir 'USNF.SMS') (Join-Path $GameDir 'USNF97.SMS') -Force
$exe = Join-Path $GameDir 'USNF97.EXE'

# Start menu shortcut, like the original installer made
$lnkDir = Join-Path ([Environment]::GetFolderPath('Programs')) "Jane's Combat Simulations"
New-Item -ItemType Directory -Force -Path $lnkDir | Out-Null
$sh = New-Object -ComObject WScript.Shell
$lnk = $sh.CreateShortcut((Join-Path $lnkDir "USNF '97.lnk"))
$lnk.TargetPath = $exe
$lnk.WorkingDirectory = $GameDir
$lnk.Save()
if ($cfg) {
    $lnk = $sh.CreateShortcut((Join-Path $lnkDir "USNF '97 Controls.lnk"))
    $lnk.TargetPath = Join-Path $GameDir 'usnf97cfg.exe'
    $lnk.WorkingDirectory = $GameDir
    $lnk.Save()
}

Step "Done"
Write-Host "    Game:  $exe   (launch this one; USNF.EXE gets Windows' Alt+Tab-blocking fix)"
Write-Host "    Log:   $(Join-Path $GameDir 'usnf97fix.log')  (written on each start)"
if ($cfg) { Write-Host "    Controls: $(Join-Path $GameDir 'usnf97cfg.exe')  (map controllers; profiles with fallback)" }
Write-Host "    For IPX network play, add IPXWrapper's DLLs to $GameDir (see README.md)."
