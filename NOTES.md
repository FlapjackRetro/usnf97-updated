# USNF '97 — Windows 11 compatibility notes

Analysis of the **v1.0 retail executable** (USNF.EXE, PE timestamp 1996-10-01) extracted from `SETUP.ESA`.
The game still needs to be re-checked after applying the 1.4 patch (`usnf14.exe`, RTPatch). That patch also updates USNF.SMS.

## Disc layout
| File | Size | Notes |
|---|---|---|
| SETUP.ESA | 19.7 MB | EA archive: USNF.EXE, USNF.SMS, USNF_1/2.LIB, WAIL32.DLL, Greenleaf CommDrv DLLs, EAREMOVE.EXE. Extract with `tools/esa_extract.py` |
| USNF_3/7/8/10.LIB | 7–430 MB | Read from CD at runtime (`X:\USNF_10.LIB` probe) |
| DIRECTX\ | | DirectX 3 redistributable. Not needed. |

## What the executable uses
- **Video:** `DirectDrawCreate` → `IDirectDraw2` (DX3). Software renderer only (no D3D).
  Cooperative level 0x53 (EXCLUSIVE|FULLSCREEN|ALLOWREBOOT|ALLOWMODEX).
  Mode table in `GG_InitMode` (0x4549f0): 320×200×8 (flip chain), 640×480×8, 800×600×8, 1024×768×8, 1600×1200×8, plus unused 640×480 15/16/24-bit.
  The whole DDraw layer is a small `CDirDraw` / `CDirDrawSurface` wrapper class (~25 methods).
- **Palette:** `WRUpdatePalette` is called from the **time thread** (every ~60 ms), not the render thread.
- **Timing:** `_InstallTimerInt` uses QPC → 1/256 s ticks with 64-bit math (no overflow). `TIMEUpdate` busy-waits for the next tick. Frame delta clamped 5..128 ticks. The time thread (`StartTimeThread`) runs at ABOVE_NORMAL, Sleep-paced at 15 ms, and also polls the joystick and screen shake.
- **CPU probe:** `_CPUSpeed@16` is a GetTickCount loop counter. It can't divide by zero or overflow on modern CPUs.
- **Input:** WinMM `joyGetPosEx` (stick, rudder, POV devices). Keyboard/mouse through window messages.
- **Audio:** Miles AIL 3.x (`WAIL32.DLL`): waveOut digital audio, XMIDI music to `midiOut`.
- **CD check:** `_cdpath` (0x42fc70) scans CD-ROM drives for `USNF_10.LIB`, **only if the `CDPATH` global (0x4c30c0, char[0x1a0]) is empty.** Nothing else writes `CDPATH`, so pre-filling it gives a clean no-CD path.
- **Multiplayer:** IPX/SPX over `wsock32.dll` (loaded dynamically in `winsock_load`), plus serial/modem through Greenleaf CommDrv. Windows 11 has no IPX, so that path needs IPXWrapper (or a code change).
- No VGA port I/O, no 16-bit thunks, no VxD use.

## Game speed bug (fixed by usnf97fix)
`TIMEUpdate` (0x40e620 in 1.0) spins until the 1/256 s clock changes, then clamps the step to **5..128 ticks** (`cmp cx,5` at +0x31).
Any frame faster than 5 ticks (≈19.5 ms, ≈51 fps) still advances the simulation by 5 ticks, so on modern hardware the game runs faster than real time.
The fix redirects the `call readClock` at +4 to a wait that returns once ≥ MinFrameTicks (default 5) have really elapsed. It sleeps instead of spinning, and it scales for slow-motion time compression (`[0x4fde08] < 0`).

## Installer
`SETUP.EXE` imports `CTL3D32.DLL` (not shipped with Windows 11) and `DSETUP.dll` (the DirectX 3 installer). `install.ps1` does what SETUP.SSF describes instead.
`usnf14.exe` is RTPatch ("EZPatch") and only uses standard Win32 DLLs.

## Window / input
- The game creates a `WS_POPUP|WS_VISIBLE` window the size of the screen, hides the cursor with `ShowCursor(0)`, and returns `SetCursor(NULL)` on every `WM_SETCURSOR`. It draws its own pointer.
- Mouse input is message based: `MainWndproc` passes WM_MOUSE* lParam straight to `MOUSEEvent`. The only other source is a single `GetCursorPos` in `CDirDraw::SetDisplayMode`.
- On `WM_ACTIVATEAPP(FALSE)` the game takes its DD lock and calls `SuspendThread` on the game and time threads. In multiplayer it calls `EndGame` instead.
- `GG_Flush` blits dirty scanline ranges from the locked system-memory surface to the primary *without* taking the game's DD lock. A suspended render thread can therefore be stopped inside a Blt.

## USNF.SMS = complete symbol map
3,440 entries (2,214 in .text incl. CRT, 1,226 data). Format: `u32 count; {u32 stroff, u32 addr}[count]; names`.
Dump: `tools/sms_dump.py USNF.SMS`. MSVC 4.x C++ mangled names plus C/fastcall/stdcall decorated names.
See `analysis/usnf_1.0_symbols.txt`.
