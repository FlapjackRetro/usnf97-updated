# usnf97-updated

Runs **Jane's U.S. Navy Fighters '97** (EA, 1996) natively on Windows 11 using your own original CD.
No game files are included or redistributed.

What you get:

- **A scripted install.** EA's `SETUP.EXE` doesn't start on Windows 11 because it needs `CTL3D32.DLL`. It also tries to install DirectX 3 drivers. `install.ps1` unpacks the CD's archive directly instead.
- **`ddraw.dll` (usnf97fix).** A small replacement for DirectDraw built for this game:
  - The game runs in a normal window or borderless fullscreen. Alt+Enter toggles between them, and the real display mode is never changed.
  - The 8-bit palette is drawn correctly, with aspect-correct scaling and black bars as needed.
  - Mouse coordinates are mapped correctly in windowed mode, and the cursor isn't hidden over the title bar.
  - **The game runs at the correct speed.** It advances its clock by at least 5/256 s per frame, so on modern PCs the simulation ran faster than real time.
  - **No CD needed** once the data files are copied to the game folder.
- **IPX network play** works through [IPXWrapper](https://github.com/solemnwarning/ipxwrapper), a third-party download (see below).

## Requirements

- The USNF '97 CD, or a mounted image of it
- `usnf14.exe`, EA's official 1.4 update, placed next to `install.ps1`
- Python 3, available as `python` (used only by the installer)
- About 700 MB of disk space

## Install

From PowerShell in this folder:

```powershell
.\install.ps1 -Disc H:\ -GameDir C:\JANES\USNF97
```

When the 1.4 patcher's window appears, confirm it. If it asks where the game is, pick the game folder. After that, start `USNF.EXE` from the game folder or from the Start menu shortcut.

If PowerShell blocks the script, run it once with `powershell -ExecutionPolicy Bypass -File .\install.ps1 ...`.

## Settings

Settings live in `usnf97fix.ini` in the game folder; every option is explained inside the file. The defaults keep the original look and behavior.

Each start writes `usnf97fix.log`. It records which fixes were applied and any DirectDraw calls the shim doesn't support. Please attach it to bug reports.

## Network play (IPX)

Windows 11 has no IPX. Download IPXWrapper and copy its `ipxwrapper.dll`, `wsock32.dll` and `mswsock.dll` into the game folder. Set `PauseInBackground=0` in the ini, because the original game ends a multiplayer session when its window loses focus.

## How the fixes work

The game was built with a symbol map, `USNF.SMS`, which it installs next to the exe. The shim reads that map to find the functions and variables it patches. Before patching anything it checks the exact instruction bytes it expects, so an unknown build is left untouched instead of crashing. See `NOTES.md` for the analysis.

## Building

```sh
./build.sh          # Linux with clang + lld; produces build/ddraw.dll
```

No Windows SDK or C runtime is needed; the declarations are in `src/win32_min.h` and `src/ddraw_min.h`.

`tests/test_patch_core.c` checks the patch matcher against a real `USNF.EXE` + `USNF.SMS` pair:

```sh
gcc -o /tmp/t tests/test_patch_core.c src/patch_core.c && /tmp/t path/to/USNF.EXE path/to/USNF.SMS
```

## Repository layout

| Path | What |
|---|---|
| `src/` | the `ddraw.dll` shim |
| `tools/esa_extract.py` | unpacker for EA's `SETUP.ESA` archives (PKWARE DCL "implode") |
| `tools/sms_dump.py` | dumps `USNF.SMS` symbol maps |
| `install.ps1` | installer |
| `usnf97fix.ini` | default settings |
| `NOTES.md` | reverse-engineering notes |
