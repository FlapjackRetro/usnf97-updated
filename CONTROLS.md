# USNF '97 keyboard reference

The retail box came with a keyboard reference card. This is a replacement for version 1.4.

Sources:
- the 1.4 executable's in-flight key handlers (`FlightKey`, `KeyStick`, `KeyThrottle`, `KeyRudder`, the view and chat code);
- the flight menu's own shortcut list (`FMENUD.MNU`);
- the game's `README.TXT`.

A key that is written in capitals, like Shift+E, needs Shift. Letters are otherwise lower case: g and Shift+G are different keys.

The menu bar (Esc in flight) has most of these as menu items as well.

## Flying

| Key | Action |
|---|---|
| ↑ ↓ ← → | Stick: nose down / nose up / roll left / roll right (only when Flight › Control › Stick = Keyboard) |
| End / PgDn | Rudder left / right (only when Rudder = Keyboard) |
| 1 2 3 4 5 | Throttle idle / 25% / 50% / 75% / 100% (military power) |
| 6 | Afterburner |
| 7 / 8 | Throttle −5% / +5% |
| g | Landing gear up/down |
| f | Flaps |
| b | Air brake and wheel brakes |
| h | Tailhook |
| x / z | Swivel the thrust nozzles 10° towards vertical / back (vectored-thrust aircraft only) |
| Shift+X | Nozzles straight down; press again for the full forward (braking) position |
| Shift+Z | Nozzles back to normal flight |
| a | Autopilot on/off (needs a working autopilot, fuel and the aircraft in a normal flight envelope) |
| Shift+E | Eject (aircraft with an ejection seat) |

The throttle keys (1–8) work only when Flight › Control › Throttle = Keyboard. With a throttle lever, the game uses the lever instead.

## Weapons and countermeasures

| Key | Action |
|---|---|
| Space | Fire the selected weapon |
| Tab | Fire the gun |
| [ / ] | Previous / next weapon |
| Insert | Chaff (the HUD shows how many are left) |
| Delete | Flare |
| j | Jammers on/off |
| Shift+J | Jettison external fuel tanks |
| Shift+K | Jettison all air-to-ground ordnance |

Space and Tab always work, whatever the Control settings are.

## Sensors and targeting

| Key | Action |
|---|---|
| Enter | Target the next visible object, left to right (front view) |
| ' | Target the visible object nearest the centre of the screen |
| ; | Clear the target |
| t / Shift+T | Next / previous radar target, by distance |
| r | Radar on/off |
| i | IR seeker on/off (aircraft with an internal IR seeker) |
| m | HARM seeker on/off (when carrying HARMs) |
| y | Radar history (trails) on/off |
| , / . | Radar range up / down |
| Shift+A | AWACS report on nearby enemy aircraft. Needs an E-2C, an E-3 or a radar picket ship in the mission, or for Vietnamese players a friendly GCI radar. |
| Ctrl+A | Point to the nearest friendly aircraft |
| Ctrl+Z | Point to the nearest enemy aircraft |
| Ctrl+X | Point to the nearest enemy ground target |
| Ctrl+T | Show target info on/off |

Ctrl+A, Ctrl+Z, Ctrl+X and target-relative views are disabled when Pref › Realistic targeting is on.

## HUD, cockpit and navigation

| Key | Action |
|---|---|
| n | HUD mode: navigation / weapons |
| Shift+[ / Shift+] | Dim / brighten the HUD |
| Backspace | Show/hide the cockpit |
| w / Shift+W | Next / previous waypoint |
| u | IFF squawk |
| d | Damage report |

## Views

| Key | View |
|---|---|
| F1 | Cockpit, forward |
| F2 | Cockpit, back |
| F3 | Cockpit, up |
| F4 | Cockpit, tracking the target |
| F5 | Player to missile (the missile threatening you) |
| F6 | Player to wingman |
| F7 | Player to target |
| F8 | Target to player |
| F9 | Fly-by |
| F10 | External, around your aircraft |
| F12 | Your missile, following it to its target |
| Ctrl + view key | The view, using your last missile instead of your aircraft |
| Alt + view key | The view, using your target instead of your aircraft (except Alt+F4, which quits) |
| Arrow keys, joystick hat | Pan the view. If you fly with the keyboard, hold Right Shift and use the arrows. |
| Num 5 | Centre the view (with Right Shift when flying with the keyboard) |
| = or Num+ / − or Num− | Zoom in / out (external views) |
| v | Copy the current view into the Other view window |

## Cockpit windows

| Key | Window |
|---|---|
| Shift+1 | Flight envelope |
| Shift+2 | Forward view |
| Shift+3 | Other view |
| Shift+4 | Target view |
| Shift+5 | RWR (radar warning receiver) |
| Shift+6 | Navigation |
| Shift+7 | System status |
| Shift+8 | Weapon status |
| Shift+9 (or 9) | Radar |

Pref › Large windows makes the windows bigger.

## Wingman commands

| Key | Command |
|---|---|
| Alt+1 | Fly straight ahead for 10 seconds |
| Alt+2 / Alt+3 | Break left / right (180°) |
| Alt+4 / Alt+5 | Break low / high (70°) |
| Alt+6 / Alt+7 | Approach the target from the left / right |
| Alt+8 / Alt+9 | Approach the target low / high |
| Alt+E | Engage my target |
| Alt+R | Engage my target, then return to formation |
| Alt+W | Engage everything of my target's type |
| Alt+P | Protect me |
| Alt+D | Disengage |
| Alt+B | Bug out (go home) |
| Alt+T | Formation: echelon / line abreast / line astern |
| Alt+H | Horizontal spacing: 500 ft / 2,000 ft |
| Alt+V | Vertical spacing: +500 ft / level / −500 ft |
| Alt+C | Formation discipline: medium / loose |
| Alt+S | Radio silence on/off |

## Game

| Key | Action |
|---|---|
| Esc | Flight menu |
| Ctrl+P | Pause |
| Shift+C | Slow motion |
| Ctrl+Q | End the mission (debriefing) |
| Alt+F4 | Quit to Windows |
| Ctrl+V | Multiplayer scores on/off |

Time acceleration (1×, 2×, 4×, 8×) is in the flight menu, under Time; it has no keys.

## Multiplayer chat

| Key | Action |
|---|---|
| ` | Message to all players |
| Left Shift+` | Message to friendly players |
| Right Shift+` | Message to enemy players |
| Alt+` | Message to your wingman |
| Ctrl+` | Message to your target |

While typing a message:

| Key | Action |
|---|---|
| Enter | Send |
| Esc (or the chat key again) | Cancel |
| Tab / ↓, ↑ | Change who receives it |
| F1–F12 | Use one of the canned messages from `CHAT.TXT` in the game folder (edit it to make your own) |

## Joystick buttons

The game uses four joystick buttons. What buttons 1–3 do depends on Flight › Control › Stick. usnf97fix sets Stick = Joystick unless you have picked one of the named stick models, so the first row is the usual one.

| Stick setting | Button 1 | Button 2 | Button 3 | Button 4 |
|---|---|---|---|---|
| Joystick | Fire weapon | Next weapon | Gun | Next missile |
| CH F-16 Flight Stick, CH Flightstick Pro, Sidewinder 3D Pro | Gun | Fire weapon | Next weapon | Next missile |
| CH F-16 Combat Stick, Jane's Combat Stick | Gun | Next weapon | Fire weapon | Next missile |

The hat pans the view, like the arrow keys. With usnf97fix, `usnf97cfg.exe` can put any key above on any controller button, hat direction or lever position (see README.md › Controllers).

## Added by usnf97fix

| Key | Action |
|---|---|
| Alt+Enter | Switch between a window and borderless fullscreen |
| Alt+Tab, Windows key | Work normally. Run USNF97.EXE, not USNF.EXE; the README explains why. |
