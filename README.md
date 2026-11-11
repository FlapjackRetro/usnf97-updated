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
  - **The intro and reference movies no longer crash.** The video player still uses the DOS-era CLI/STI instructions, which Windows 9x tolerated but NT-based Windows doesn't.
- **Controllers through DirectInput 8 and XInput.** Use any mix of controllers plus the keyboard. Stick, throttle, rudder pedals, hat and buttons can come from different devices, and anything left unmapped stays on the keyboard. Extra buttons, hat directions and lever positions can press any game command, such as gear, flaps, views or ThrottleUp. See *Controllers* below.
- **Network play**: 1.4 has TCP/IP built in. IPX play works through [IPXWrapper](https://github.com/solemnwarning/ipxwrapper), a third-party download (see below).

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

When the 1.4 patcher's window appears, confirm it. If it asks where the game is, pick the game folder. After that, start **`USNF97.EXE`** from the game folder or from the Start menu shortcut.

`USNF97.EXE` is an identical copy of `USNF.EXE` with a different name, plus `USNF97.SMS` (the game loads the symbol file named after its exe). It exists because Windows' compatibility database applies a fix called *IgnoreAltTab* to any program named `USNF.EXE`. That fix swallows Alt+Tab and the Windows key. The database's other fix for this game (CLI/STI and division-overflow handling) is built into usnf97fix, so the copy loses nothing. If you re-patch `USNF.EXE`, copy it to `USNF97.EXE` again.

If PowerShell blocks the script, run it once with `powershell -ExecutionPolicy Bypass -File .\install.ps1 ...`.

## Settings

Settings live in `usnf97fix.ini` in the game folder; every option is explained inside the file. The defaults keep the original look and behavior.

Each start writes `usnf97fix.log`. It records which fixes were applied and any DirectDraw calls the shim doesn't support. Please attach it to bug reports.

## Wide flight view

Set `WideFlight=1` in `usnf97fix.ini` and choose 640×480 under Pref → Screen resolution. Flight then runs at a 480-line resolution as wide as your screen's aspect ratio (1704×480 on 32:9, 1144×480 on 21:9). You get the same vertical view as 640×480 plus more to the sides, with the original high-res cockpit art. Menus stay 640×480; the bars beside them are filled per `SideFill`. Use Alt+Enter for borderless fullscreen.

## Detail and draw distance

All off by default; set them in `usnf97fix.ini`.

- `RenderScale=2` renders the in-flight 3D scene at twice the game's resolution. The cockpit, HUD and menus keep their original art. It needs a 640×480 flight resolution (or the wide view). 2 is the most the 3D engine's tables allow.
- `ForceHighDetail=1` makes ships, aircraft and buildings always use their most detailed model.
- `TerrainDetail=2` (up to 4) extends detailed, textured terrain and buildings that many times farther. The cost grows roughly with the square.
- `ObjectRange=1` keeps ships, aircraft and vehicles drawn at any distance.

Haze and fog from the mission's weather still limit how far you can see.

## Controllers

The game only knows the 1996 Windows joystick API: one stick, throttle, rudder, one hat and four buttons. usnf97fix redirects those calls to a virtual joystick fed from DirectInput 8 controllers (HOTAS, flight controllers, pedals) and XInput pads.

1. Start the game once and open `usnf97fix.log`. It lists every controller with its axes, buttons and hats, plus the mapping that `auto` chose.
2. Adjust `[input]` in `usnf97fix.ini` if needed. For example:
   ```ini
   Roll=Echo:X
   Pitch=Echo:Y
   Throttle=Echo:S1~
   Rudder=Echo:RZ
   ```
   To find what your controller calls each control, set `LogInput=1`, move every control, and read the log.
3. In the game, open Flight › Control. For each part you mapped, choose Stick = Joystick (or a stick preset), Rudder = Rudder pedals, and Throttle = Throttle stick. Parts you didn't map stay on Keyboard.
4. Bind anything else in `[keys]`, for example `Gear=Echo:B9`, `Flaps=Echo:B10`, or `Afterburner=Echo:S1<5`. All commands are listed in the ini.

Xbox-style pads work automatically: left stick for pitch/roll, triggers for rudder, RB/LB for throttle up/down, d-pad as the hat, and A/B/X/Y as buttons 1–4.

`StickResponse` (percent, default 100) speeds up how quickly the aircraft follows the stick and stops when you centre it; the original flight model eases in and out, which feels laggy on a modern controller.

`JoystickPollMs` controls how often the game reads the joystick. The game's own interval is 50 ms; usnf97fix defaults to 10 ms to cut input lag (the virtual joystick is cheap to read).

## Network play

Version 1.4 includes TCP/IP play; use it first. Set `PauseInBackground=0` in the ini, because the original game ends a multiplayer session when its window loses focus.

For IPX, which Windows 11 lacks, download IPXWrapper and copy its `ipxwrapper.dll`, `wsock32.dll` and `mswsock.dll` into the game folder.

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
