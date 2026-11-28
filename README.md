# BHD Player Config

A `winmm.dll` proxy with quality-of-life improvements for **Delta Force: Black Hawk Down** (`dfbhd.exe`). The DLL forwards Windows multimedia calls to the original 32-bit system WinMM library and applies only the options enabled in `bhd_player_config.ini`.

## Installation

1. Copy the **32-bit** `winmm.dll` to the same directory as `dfbhd.exe`.
2. Copy `BHD Player Config.exe`, `bhd_player_config.ini` and `localres.pff` to the same directory.
3. Open `BHD Player Config.exe`, adjust the options and click **Save**.
4. Launch the game normally. Restart it after changing settings.

Example:

```text
Game directory/
├── dfbhd.exe
├── winmm.dll
├── BHD Player Config.exe
├── bhd_player_config.ini
└── localres.pff
```

The configurator always reads and writes `bhd_player_config.ini` beside its own executable. The DLL reads that same filename from the game directory. Keep the DLL, configurator and INI together. The INI file is optional; if it is missing, the DLL uses the default values shown below. The configurator can create it by clicking **Save**. You can also edit it manually, using `1` to enable an option and `0` to disable it.

The application includes **Settings**, **Crosshair**, and **HUD** tabs. Settings contains the boolean DLL options. Crosshair provides style and color selectors for 80 generated crosshairs and a 16:9 preview. HUD selects one of the four layout modes.

The Crosshair tab works directly with `coma20mm.tga` inside `localres.pff`, which must be beside `BHD Player Config.exe`. **Load Current Crosshair** reads the current image from the archive and recognizes matching generated choices; other images appear as a custom/current crosshair. Applying a choice rebuilds a temporary PFF beside the original, verifies the replacement and then replaces `localres.pff`; it does not create a separate TGA or installer. Close the game before applying so it is not holding the archive open. The configurator supports the BHD PFF3 layout with 36-byte entry records and preserves the other archive entries and trailing data.

When upgrading from this project's earlier `dinput8.dll` proxy, remove that DLL
before installing `winmm.dll`, so the patches are not applied twice.
Copy your previous settings from `dinput8.ini` to `bhd_player_config.ini`. The new proxy does
not read `dinput8.ini`; another mod can use that filename independently. A
`dinput8.dll` supplied by another mod can coexist with this proxy, although mods
that patch the same game code still require compatibility testing. Only one
`winmm.dll` proxy can occupy the game directory.

## `bhd_player_config.ini` options

### `[PatchGroups]`

| Option | Default | Description |
| --- | :---: | --- |
| `UseCorrectAspectFOV` | `1` | Corrects the field of view for internal render resolutions wider than 4:3 without changing zoom, scopes, or special cameras. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `MousescaleFix` | `1` | Makes a bare `mousescale` query safe and preserves `mousescale N` changes across mission loads. |
| `AdaptiveScreenCenter` | `1` | Uses the current game window's client center for cursor recentering and relative movement when the game uses its original mouse input routine. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Uses the full game client area when the active game confines the cursor. Restores confinement after Alt+Tab and window or display changes while honoring explicit releases. Works independently of `RawMouseInput`. |

#### Experimental

| Option | Default | Description |
| --- | :---: | --- |
| `BorderlessFullscreen` | `1` | Runs the game in a borderless window that covers the entire monitor. |
| `ForceDesktopResolution` | `1` | With `BorderlessFullscreen=1`, uses the monitor resolution as the internal render resolution. At `0`, the resolution selected in the game is stretched to fill the screen. Has no effect when borderless mode is disabled. |
| `HUDLayoutMode` | `1` | Applies to the HUD and in-game menus/windows (Armory, G briefing/objectives, F1 shortcuts, K players/kill list). `0`: original scaling; `1`: original scaling with native-size crosshair; `2`: centered 4:3 HUD and windows; `3`: widescreen-anchored HUD groups with centered 4:3 windows. The compact player/team counter stays anchored to the right edge in modes 2/3. Modes 1-3 keep the crosshair at its native pixel size. |
| `AllowHighResolutionFonts` | `1` | Detects the atlas dimensions of each loaded FNT0 file independently, allowing stock and higher-resolution fonts to coexist. Does not create or replace font assets. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `HighFrequencyCamera` | `1` | Applies Raw Input movement that arrived after the latest game update to the local first-person presentation camera. Requires `RawMouseInput=1`. |
| `HighFrequencyCameraPosition` | `0` | Interpolates on-foot first-person camera XYZ to smooth static scenery while walking/strafing. Independent of Raw Input and mouse rotation. Adds up to one movement update of visual position lag. |
| `HighFrequencyCameraPositionExtrapolation` | `1` | Predicts the first-person camera position using the last movement step. Takes precedence over `HighFrequencyCameraPosition`. |
| `HighFrequencyViewmodel` | `1` | Makes the local first-person weapon and hands follow the presentation camera, with interpolation of weapon-relative motion and bones. Requires either camera position option, or both `HighFrequencyCamera=1` and `RawMouseInput=1`. |
| `SmoothAnimationBlendInterruptions` | `1` | Preserves the current pose and root motion when an on-foot local-player movement blend is interrupted, while retaining native blend durations when adaptive duration is disabled. |
| `AdaptiveBlendDuration` | `1` | Shortens interrupted blends according to the existing destination state weight. Requires `SmoothAnimationBlendInterruptions=1`. |
| `RemotePlayerInterpolation` | `1` | Interpolates the on-foot third-person bodies of remote players on a multiplayer client. Adds up to 100 ms of visual delay and is independent of the camera and Raw Input options. |
| `RemotePlayerAnimationInterpolation` | `1` | Blends the rendered skeletal poses of remote players on foot between consecutive game updates. Can run independently of `RemotePlayerInterpolation`. Adds up to one game update of pose delay (normally 16 ms). |

`AllowHighResolutionFonts` accepts square, 32-bit FNT0 pages of 256, 512, 1024,
2048 or 4096 pixels per side, with up to 16 equally sized pages per file. A
256x256 font can coexist with a 1024x1024 font. Detection uses the exact file
length reported by the game's loader for loose files and PFF resources; it runs
when a font loads, never while drawing text. Invalid tracked files are rejected.
Set the option to `0` and restart to retain the unmodified native font loader.

The supplied pixels are uploaded at their detected dimensions through the native
texture manager. Normalized UVs, reference width, spacing and logical text size
remain defined by the FNT. HD replacements must preserve those logical metrics
to retain the original layout. No vector fonts, sharpening, font substitution or
aspect-ratio correction are applied. The device's texture limits can reduce the
actual GPU texture resolution.

The native font loader also has an optional CPU drawing path which assumes a
256x256 pixel buffer. For HD files, the DLL stages a logical 256x256 mirror for
that path and redirects GPU uploads to the original HD pages. This avoids
misreading subsequent pages or changing CPU-buffer ownership. If an HD GPU upload
fails, the same logical mirror supplies a lower-resolution fallback. Original
256x256 FNTs use the original loading path without a staging copy.

See [dynamic FNT loading](docs/dynamic_fnt_loading.md) for format requirements,
implementation details and the automated/manual validation status.

`RawMouseInput` receives mouse packets on a dedicated message thread so movement
can reach presentation frames between the game's fixed simulation updates. Each
`WM_INPUT` event is read directly and then `GetRawInputBuffer` reads queued reports
in bounded batches. The buffered reader handles the different packet layout used
by a 32-bit game on 64-bit Windows (WOW64). Only relative movement uses Raw Input;
buttons, wheel and menu cursor coordinates keep the game's native Windows message
path. The registration neither captures activation clicks nor receives background
input, so other applications can be activated normally.
`HighFrequencyCamera` projects the game's native two-update mouse smoothing, then
uses the current sensitivity, vertical inversion, and scoped-view divisor. The
temporary visual angles are restored immediately after the camera is built; the
simulation consumes each movement once through its original path. The feature
currently applies to the local player in first person. Third-person, vehicle,
and spectator cameras retain their native behavior.

Both camera position options use the last two local movement samples and the
game's render phase to smooth on-foot first-person movement. They adjust the
rendered camera position and preserve native eye-height and camera offsets.
Both work independently of `RawMouseInput` and `HighFrequencyCamera`.

| Test mode | `HighFrequencyCameraPosition` | `HighFrequencyCameraPositionExtrapolation` |
| --- | :---: | :---: |
| Interpolation (default) | `1` | `0` |
| Extrapolation | `0` | `1` |
| Native camera movement | `0` | `0` |

Restart the game after switching modes. Interpolation adds up to one movement
step of visual delay, normally 4 ms on the fast path or 16 ms on the supported
slow path. Extrapolation projects forward for less than one step. It can overshoot
when you stop, reverse direction or hit a wall. If both options are enabled, the
game uses extrapolation.

Pauses, focus loss, target/mount changes, teleports, clock discontinuities and
unobserved actor motion invalidate the motion history. Installation checks all
three hook locations before writing any of them.

`HighFrequencyViewmodel` combines the presentation camera with interpolated
weapon-relative motion and bone transforms in a private palette. Position
smoothing is shared with the weapon, including when raw mouse rotation is off.
History is rebased across position discontinuities. Weapon simulation, firing
and networking remain native.

#### Complete default configuration

```ini
[PatchGroups]
BorderlessFullscreen=1
ForceDesktopResolution=1
UseCorrectAspectFOV=1
HUDLayoutMode=1
AllowHighResolutionFonts=1
RawMouseInput=1
HighFrequencyCamera=1
HighFrequencyCameraPosition=0
HighFrequencyCameraPositionExtrapolation=1
SmoothAnimationBlendInterruptions=1
AdaptiveBlendDuration=1
RemotePlayerInterpolation=1
RemotePlayerAnimationInterpolation=1
HighFrequencyViewmodel=1
MouseScalingFix=1
MousescaleFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

```

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The build produces `winmm.dll` and `BHD Player Config.exe`. CMake also copies
`bhd_player_config.ini` into the build directory. A 64-bit build is rejected because
`dfbhd.exe` is 32-bit.

Run the regression suite with:

```sh
ctest --test-dir build --output-on-failure
```

The WinMM integration test imports the SDK's multimedia API normally and verifies
that the loader selects the built proxy. It checks all 193 named/ordinal exports,
concurrent clock calls, the game's nine WinMM APIs, timer callbacks and additional
wave/multimedia file calls against the system library. Run the math tests in
Debug to keep their assertions enabled.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied.

Patch initialization runs once from the supported game's verified startup
`timeGetTime`/`timeGetDevCaps` call sites (`0x00569F7A`/`0x00569FC5` return
addresses). Calls made by other DLLs during loading only forward to native
WinMM. `DllMain` performs no library loading, patch installation or thread waits.
An executable with different startup call sites needs explicit compatibility
support before these patches can initialize. No executable modification is
required for the supported version, which imports `WINMM.dll` directly.


### Smooth animation blend interruptions (experimental)

Set `SmoothAnimationBlendInterruptions=1` in `[PatchGroups]` or enable the
Movement checkbox in BHD Player Config before launching the game. Default: on.
For supported on-foot local-player locomotion blends, interrupted transitions
capture the current raw BAD pose and five root-motion/collision-bound values
instead of restarting from the old primary animation. Target animation events
and movement integration are unchanged. Native durations remain unchanged
when adaptive blend duration is disabled. The fix covers
all eight directions in standing run/walk, crouched and prone locomotion,
standing/crouched/prone idles, both alternate forward runs (0x64/0x65),
left/right rolls (0x5F/0x60) and jump start/loop (0x72/0x73).
Native roll rotation, jump physics and queued animation completion remain
handled by the game. Remote players, vehicles and unsupported animation
layouts retain native behavior.
The DLL checks x86 call sites and native function signatures before patching.
This option changes real movement and is experimental; test offline first and
use consistently in multiplayer only with the server/community's approval.

### Adaptive blend duration (experimental)

Set `AdaptiveBlendDuration=1` in `[PatchGroups]` or use the Movement checkbox.
Requires `SmoothAnimationBlendInterruptions=1`; otherwise it has no effect.
For a successfully captured interruption, the native 9- or 13-tick duration
is multiplied by (1 - existing destination-state weight), rounded to the
nearest tick and clamped to at least one tick. Normal transitions and unknown
state histories retain their native duration. Default: on when the parent fix
is enabled. Test offline first.
