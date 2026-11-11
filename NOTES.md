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

## 1.4 patch (usnf14.exe → USNF.EXE 1998-12-07, USNF.SMS 3,560 symbols)
- Imports: unchanged DLL list. New user32 imports `ClipCursor`, `SendMessageA`, `SetDlgItemTextA` and `SetRect`; no DirectInput.
- New code is almost entirely networking (TCP/UDP lobby, player lists), plus a JPEG decoder and `ShutdownJoysticks`.
- `TIMEUpdate` is byte-identical to 1.0 (same 5-tick clamp). The patch notes' "frame rate limited to 52 fps" is that clamp (256/5 = 51.2): it fixes the *step*, not the frame time, so fast machines still run fast. The usnf97fix speed patch matches 1.4 unchanged (TIMEUpdate 0x40e8d0, clock 0x40ea20, now 0x51160c, last 0x511660, compression 0x511618).
- `CDPATH` is at 0x4d63c8 (416 bytes).

## Input (1.0 and 1.4)
- WinMM only: `joyGetNumDevs`, `joyGetDevCapsA`, `joyGetPos` and `joyGetPosEx`. Up to 16 devices are scanned.
- `InitJoysticks` assigns each control to the first device that has it: X/Y stick (`joystickXDevice`/`YDevice`), throttle (JOYCAPS_HASZ), rudder (HASR) and POV hat (HASPOV). Pedals or a throttle on a separate device therefore work.
- Buttons: only the first four are used. Button 4 = next missile. Buttons 1–3 map to gun / weapon release / weapon-cycle (a fake keypress), in an order that depends on Flight › Control › Stick (`_stickDevice`, `PotStick` switch):

  | Stick setting | value | button 1 | button 2 | button 3 |
  |---|---|---|---|---|
  | Keyboard / Joystick | 0 / 1 | weapon | cycle | gun |
  | CH F-16 Flight Stick | 2 | gun | weapon | cycle |
  | CH F-16 Combat Stick | 3 | gun | cycle | weapon |
  | CH Flightstick Pro | 4 | gun | weapon | cycle |
  | Microsoft Sidewinder 3D Pro | 5 | gun | weapon | cycle |
  | Jane's Combat Stick (**new in 1.4**) | 10 | gun | cycle | weapon |
- The POV hat is converted to arrow-key presses (`GetPOV`: 0x48/0x4D/0x50/0x4B).
- Rudder: Keyboard or Rudder pedals. Throttle: Keyboard or Throttle stick. Throttle calibration (`InputCalibrate`) asks for the afterburner detent.
- `InitJoysticks`, `ReadDevice`, `NormalizeStick`, `PotThrottle` and `GetPOV` are logically unchanged in 1.4 (only relocations and register allocation differ).

## CLI/STI in the video player (fixed by usnf97fix)
`MainLoop` (the Cobra video loop run by `PlayCobra` for the intro movies and reference videos) reads the clock between `cli`/`sti`. It has 13 CLI and 14 STI in both 1.0 and 1.4. NT-based Windows raises STATUS_PRIVILEGED_INSTRUCTION for these, and the game's own handlers only cover divide/overflow/access violation. usnf97fix installs a vectored handler that skips a faulting FA/FB byte inside USNF.EXE's image.

## Joystick internals (for the input layer)
- `ReadDevice` reads a device at most every 50 ms (`sub eax,ecx; cmp eax,32h; jae` after `timeGetTime`). It uses `joyGetPosEx(JOY_RETURNALL)` when the caps report ≥3 axes or ≥4 buttons, otherwise `joyGetPos`.
- `InitJoysticks` scans `joyGetNumDevs()` devices. A device must pass both `joyGetDevCapsA` and `joyGetPos`. X/Y are taken from the first device whose X range is non-empty; throttle needs JOYCAPS_HASZ, rudder HASR, hat HASPOV.
- `NormalizeStick` scales each axis using the caps min/max to ±256. It takes the **first** stick reading as the centre.
- Throttle (`PotThrottle`) = −normalized Z, so minimum Z = full power. `InputCalibrate` stores the afterburner detent.
- Keyboard: `KEYEvent` uses the **scancode** from lParam (bits 16–22) and tracks Shift (2A/36), Ctrl (1D) and Alt (38) itself. Held keys go to `_keyarray[scancode]`: arrows = stick, End/PgDn = rudder, Space = weapon, Tab = gun. Throttle keys: 1 idle, 2/3/4 = 25/50/75 %, 5 full, 6 afterburner, 7 −5 %, 8 +5 %.
- Flight menu accelerators (FMENUD.MNU): F1–F10 and F12 views, Shift+1–9 windows, Ctrl+P pause, Shift+C slow motion, Ctrl+T target info, Alt+S radio silence, Ctrl+Q end mission, Backspace cockpit. Single-key commands (FlightKey): a autopilot, b brakes, d damage, f flaps, g gear, h hook, j jammer, t target, [ ] weapon select, E eject, Insert chaff, Delete flare.

- **Device scans block.** `IDirectInput8::EnumDevices` takes about 1.2 s on the test machine with a Honeycomb Echo attached. The hot-plug rescan used to run every 2 s on the polling thread, which froze the controller state for about 1.2 s out of every 3.2 s and showed up as a "lagging" stick. It now runs on its own thread (`enum_thread`), and the poll thread only applies a changed device list. With `LogInput=1`, the log reports scan times and any poll-thread stall over 60 ms. It also logs the game's `_stickX`/`_stickY` each frame (via a hook on `FlyingLoop`'s `GetPlayerControl` call).
- Flight model: `StickInput@28` eases each control towards the stick at a per-aircraft onset rate and returns at a recentre rate (F-14: onset 20, recentre 8). `StickResponse=N` scales both by N %.

## Cockpit art (canopy frames)
- `.HUD` files (in usnf_2.lib) are small PE images. Their struct at file+0x200 is copied to `_hud`. `+1` holds the low-res canopy name (`~f14`), `+0xE` the high-res one (`~f14h`), `+0x28` the low-res hotspot (F14: 349,114) and `+0x2C` the 640×480 hotspot (F14: 640,240).
- Canopy PICs are type 1 (span lists, see `tools/pic2png.py`). `~F14H` is **1280×490**, twice the width of the 640×480 view.
- `HUDDraw` blits the canopy at 1:1 (no scaling) with its hotspot on the HUD centre, which is the projected boresight. The HUD centre shifts with G-load (head bob), so the art has margin beyond the screen edges.
- `HUDInit`: 640×480 loads `~xxxh` with the 640 hotspot. Any other mode loads `~xxx`, and if the width isn't 320 it **frees it again**: 800×600, 1024×768 and 1600×1200 draw no canopy frame at all.
- At 640×480 the visible canopy columns are 320–959. A 16:9 view at 480 lines (854 wide) would show columns 213–1066, all inside the art.

## Windows compatibility database entry (why the game runs as USNF97.EXE)

`C:\Windows\apppatch\sysmain.sdb` has an entry for `USNF.EXE` (app "USNF '97", Electronic Arts; matched by exe name plus sibling files CDRVDL32/CDRVHF32/CDRVXF32.DLL, USNF_1.LIB, WAIL32.DLL, BRIEFING.TXT). It applies two shims:

- **IgnoreAltTab** (AcGenral.dll, include modules DINPUT/DINPUT8/DINPUT9). In practice Alt+Tab and the Windows key are swallowed system-wide while the game is foreground. Windows takes the key (the game never gets the Tab or VK_LWIN) and then does nothing. This happens with or without DirectInput loaded in the process.
- **USNF97** (AcSpecfc.dll, `NS_USNF97::USNF97_ExceptionFilter`). If the faulting instruction is FA/FB (CLI/STI), it skips it. If it is F7 (DIV/IDIV, optionally after a 1-byte prefix), it sets EDX = 0, makes EAX positive and retries. usnf97fix's vectored handler does both (division only for STATUS_INTEGER_OVERFLOW, since retrying a divide-by-zero would loop).

Diagnosis notes: message tracing showed the subclassed window got Alt but never Tab or VK_LWIN, and the window thread stayed responsive (the hang watchdog in hangwatch.c never fired). apphelp.dll, AcGenral.dll and AcSpecfc.dll were loaded with no compatibility mode set in Properties. Renaming the exe stops the match. The game then needs `<exe>.SMS`: SMInit builds the symbol-map name from the module file name and reports a missing file as "Not enough memory."

## To do

- **32:9 flight view: done** (`WideFlight=1`, see "Wide flight view" below). Not yet checked: other aircraft cockpits, external/padlock views, MFD view windows, multiplayer.
- **Higher internal resolution for the 3D view: done** (`RenderScale=2`, see below). 3× would need `_lineStats` relocated (1200-line table, followed by the texture mapper's state).
- *(original plan)* **Higher internal resolution for the 3D view.** Decision: the cockpit art and the 2D HUD/GUI stay as they are (original 480-line art, scaled up as now); only the 3D scene renders at a higher internal resolution (e.g. 2×/3× of 1706×480). Open questions:
  - Can the 3D pass and the 2D overlays be separated (render 3D large, then composite the original cockpit/HUD scaled)?
  - How does the software rasterizer size its buffers and projection, and what hard limits does it have (1600-wide buffers)?
  - How does the 8-bit software renderer perform at high resolution?
- **Echo preset:** needs a log with the controller connected and `LogInput=1`.
- **External controls configurator.** The in-game Flight › Control screen only picks a stick type, rudder source and throttle source. It can't express the per-device mappings, extra buttons, lever taps and key commands usnf97fix supports, and players must currently set it by hand to match `[input]`. Build a standalone tool (run from the shortcut or before launch) that:
  - Layout: a picker at the top lists the detected controllers (DirectInput and XInput). Choosing one fills the middle panel with a live readout of everything that device reports: every axis and slider as a bar with its percentage, every button as a lamp, and every hat as a compass. Each updates in real time as the user moves or presses it, so they can see for themselves which physical control is which number (for example, "the speed brake is Z" or "the trim wheel is B23/B24"). The bottom panel lists USNF97's functions: Roll, Pitch, Throttle, Rudder, Hat, Fire weapon, Gun, Weapon cycle, Next missile, plus every [keys] command such as Gear, Flaps, Brakes, Hook, Target, Chaff and Flare. The user assigns a control to each function, either by picking it or by clicking the function and then moving or pressing the control. Options per assignment: invert, deadzone, threshold for axis-as-button, and tap (`!`) for lever positions.
  - Writes `[input]` and `[keys]` in `usnf97fix.ini`. It also sets the game's own control settings to match (Stick = Joystick, Rudder = pedals, Throttle = throttle stick when those parts are mapped, otherwise Keyboard), so nothing has to be changed in-game. First find where the game stores those settings: `_stickDevice` and the rudder and throttle choices, plus the afterburner detent from `InputCalibrate`. This could be a pilot or config file or the registry. Alternatively, usnf97fix could force the values at startup.
  - Keeps per-controller profiles keyed by device name/GUID and ships presets (Xbox pad, Honeycomb Echo).
  - Handles missing controllers. When a previously configured device isn't plugged in, usnf97fix falls back at launch to the next profile whose devices are all present, or else to keyboard or XInput defaults. It also switches the game's control settings back to Keyboard for the parts that lost their device, so the game doesn't wait on a joystick that isn't there. The fallback order is set in the configurator.

## Wide flight view (src/widescreen.c)

The 3D viewport, HUD centre (the boresight projected through the 3D engine), dialogs (`_TopCenterDialog`), cockpit panels (`CPInit`, right/bottom anchored) and mouse limits all follow the screen size already. What had to change:

- **Mode.** Flight and menus share mode 2 (640×480), and then `usnfmain` never switches modes. The flight mode (`0x52eb88` in 1.4) is read three times in `usnfmain`; a stub returns new mode 0x20 instead of 2. `GG_GetMode` fills `_grMode` for 0x20 (W×480, aspect 0x7fff), and `GG_InitMode` calls `SetDisplayMode(W,480,8)`.
- **Width multiple of 8.** The game copies frames assuming row length == width. Our surfaces pad rows to 4 bytes, so 1706 gave sheared 2D layers. W = 1704 on 32:9.
- **Projection.** `render_3d` normalizes to the larger screen axis (aspect values `_aspectx/y/z` scale the view matrix; `sx = x·ax·scrw/(z·az)`), so a wider screen zoomed in (vert-). Patched to compute the aspect values with min(scrw, scrh·4/3), then scale `_aspectx` by that / scrw. Result: hor+, with the culling FOV (`field_of_view`) and the horizon corners (`_SolidHorizon`) consistent.
- **Cockpit.** `HUDInit` uses the high-res art, hotspot and positions only for exactly 640×480; its four 640 constants become W.
- **Haze spans.** `G_SUPolygon` (Gouraud, used for the horizon haze) counted 4-pixel groups in CL, so spans over 1023 px wrapped (1704 drew as 680). Now `dec cx`; ECX's low word is otherwise unused there. The textured span routines also count in CL but use ECX's high bits for texture coordinates; their spans are short perspective segments, so they are unaffected.

## Higher internal 3D resolution (src/hires.c)

- FlyingLoop's `T_Render(&_mainV)` is redirected. It first renders into our own 2× bitmap (game layout: +2 w, +6 h, +0A pixels, +22 row table; `_cb` is swapped, the clip box set to the full size, and `_sizeAdjust` multiplied by 2 inside T_Render's `GRRender` call), then renders normally. Because the normal pass is last, every projection global and the visible-target list are unchanged.
- After the normal render, the frame is snapshotted. At `GG_Flush` (entry hooked with a 6-byte trampoline), pixels that differ from the snapshot were drawn by 2D layers and are scaled 2×; the rest comes from the 2× render. The presenter shows that composite.
- Limits: per-scanline tables are sized for 1200 lines. `_lineStats` (dirty lines, 1296 bytes) is directly followed by `_gbuffer`, the texture mapper's live state, so a 1440-line pass would corrupt it. Polygon YLR entries are 6 bytes/line (~3300 lines).

## Model detail (ForceHighDetail)

Models are bytecode. `do_jumpfar4` operands are [size][threshold][offset]: if size·aspecty·scrh/depth < threshold·`_sizeAdjust`, it jumps to the low-detail part. NOPing that `jb` keeps the offset 0. `_effects & 0x20000` bypasses the test too (probably the reference viewer).

## Terrain and draw distance (src/terrain.c)

- `T_Make` rebuilds a terrain quadtree per frame. Root cell size (`0x5030e0`) and texture level (`0x503094`: 1/2/4/8) come from altitude.
- A node of size s is split when its distance < f(s, terrain-detail pref). Nodes "behind" (heading zones, ±60° around each axis) get 1/8 of that, and nodes beyond the weather layer's range (`_currentLayer+22h`) are dropped.
- Only nodes ≤ the texture level are textured. Buildings and airfields (`T_SortTmapList`) attach only to nodes ≤ 4. Everything else is a flat polygon.
- Capacities (all bounds-checked by the game except the node array):
  - node array: 499 × 22 bytes, reached only through the pointer at `0x4d0048`; add limit 1F3h, split limit 1F0h
  - terrain command list: 10000 bytes, set by immediates in `T_InitDatabase`
  - main view object list: `VIEWInit(&_mainV, 10000)` in `usnfmain`
  - render_3d sort list (~350 objects) and object buffer: immediates in `render_3d` and `GRAddBrentObj`
- `TerrainDetail=N` multiplies the split distance by N (stub after it is computed) and every capacity above by N².
- Moving objects are culled per type in `T_AddYourObjs` (distance vs type+37h); `ObjectRange=1` removes that `jg`.
- Wide view: T_Make's visible box uses the view triangle at a fixed ±45° (corners ±80Eh, 80Eh ahead, rotated by heading). The x components are scaled by W/640, and the behind-zones are narrowed by the extra half-angle (60° → ~36° at 1704).
