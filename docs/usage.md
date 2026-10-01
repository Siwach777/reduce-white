# Usage and platform behavior

## Opacity and state

Opacity controls the black overlay, not the display's backlight. At 0 the software effect is absent, at 0.3 it is a 30% black overlay, and at 1 the overlay is fully opaque. Increase makes the screen darker; decrease makes it lighter.

The daemon stores a configured opacity and an active flag. Toggling off preserves the configured opacity, so toggling on restores it. Setting an opacity or changing it incrementally activates the effect. A fresh daemon starts at opacity 0.30 with the active flag set. State is held in memory and resets when the daemon restarts; it is not persisted to a configuration file.

The overlay uses an 8-bit alpha channel. Closely spaced opacity values can produce the same pixels; those changes are stored but do not trigger a repaint. Status prints two decimal places. The reported active flag indicates command state, even when a configured opacity of 0 produces no visible dimming.

## OLED and LCD displays

The same software overlay can be used on either display technology. LCD panels use a backlight, while OLED pixels emit their own light; [Samsung Display describes the distinction and content-dependent OLED power measurement](https://global.samsungdisplay.com/31053?type=main). This utility changes the composed image. It does not directly lower an LCD backlight unless Linux hardware control is enabled.

An OLED's light output and power usage depend on its pixels, content, hardware settings, and display pipeline. Overlay opacity is not a calibrated luminance or power percentage. The project does not measure battery savings, control the panel's PWM behavior, or establish a reduction in burn-in. HDR and color-managed content also require visual testing with the target compositor.

Start with a subtle opacity and adjust it to your display:

```bash
reduce-white --set 0.10
reduce-white --decrease 0.05
reduce-white --toggle
reduce-white --status
```

Use `--set 0` or `--quit` to remove the software effect. Keep a keyboard shortcut for either command if experimenting with high opacity; opacity 1 makes the overlay completely black.

## Command reference

Use one control command per invocation. Options have long forms and the aliases shown below.

| Command | Aliases | Behavior |
| --- | --- | --- |
| `--help` | `-h` | Print usage without starting Qt's GUI or contacting a daemon. |
| `--version` | `-v` | Print the application version. |
| `--daemon` | `-d` | Run the daemon in the foreground. Cannot be combined with a control command. |
| `--ddcutil` | None | Enable Linux hardware control for a newly started daemon. |
| `--set VALUE` | `--set=VALUE` | Set opacity to a finite value between 0 and 1, including endpoints. |
| `--toggle` | `-t` | Toggle active state, preserving the configured opacity. |
| `--increase [STEP]` | `-i` | Increase opacity; default step 0.05. |
| `--decrease [STEP]` | None | Decrease opacity; default step 0.05. |
| `--get` | `--status`, `-g` | Print configured opacity and active state. |
| `--quit` | `-q` | Stop a daemon and clean up its IPC socket/lock. Succeeds if none is running. |

Examples:

```bash
reduce-white --set 0.25
reduce-white --set=.25
reduce-white --increase 0.1
reduce-white --decrease
reduce-white --toggle
reduce-white --status
reduce-white --quit
```

Decimal values, a leading plus sign, and scientific notation are accepted, for example `0.25`, `.25`, `+0.25`, and `2.5e-1`. The decimal separator is always a dot, independently of the system locale. Group separators, NaN, infinity, hexadecimal floating-point values, trailing text, and incomplete values are rejected.

A step must be greater than 0 and at most 1. The resulting opacity clamps to 0 or 1. An explicit `--set` value outside that range is an error rather than a silent clamp.

```bash
reduce-white --set 0.5junk       # Invalid; no state change
reduce-white --set nan          # Invalid; no daemon auto-start
reduce-white --increase -0.1    # Invalid step
reduce-white --get --toggle     # Conflicting commands
```

## Daemon startup and shutdown

A control command contacts the current user's daemon. If no daemon is available, it starts the same executable as a detached `--daemon` process and waits for startup. `--quit` does not auto-start a daemon. `--get` does auto-start one when necessary, so it is not a read-only process-existence probe.

A connected request has a one-second IPC deadline. On Windows, Qt's initial connection setup can wait up to five seconds if every named-pipe instance is busy. Auto-start polls approximately every 20 ms for up to three seconds; process creation and the final request can add time. A failed connected request is not automatically replayed because a toggle or increment may already have changed state.

For visible startup errors or hardware diagnostics, run in the foreground:

```bash
reduce-white --quit
reduce-white --daemon
```

Normal detached startup redirects standard streams to the null device. A second daemon for the same user/instance exits with an error. Crashed daemons leave a socket/lock that a new lock owner can recover; a live daemon's lock is not stolen merely because it has been idle.

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | Successful command, help/version, or quitting a nonexistent daemon. |
| 1 | Startup, locking, socket, timeout, acknowledgement, or runtime failure. |
| 2 | Invalid CLI syntax, conflicting flags, unsupported option, or invalid number. |

## Hardware brightness

Hardware control is available only on Linux and must be enabled at daemon startup:

```bash
reduce-white --quit
reduce-white --daemon --ddcutil
```

To start a new detached daemon with hardware control:

```bash
reduce-white --set 0.3 --ddcutil
```

Passing `--ddcutil` to an already running daemon does not alter its configuration. Restart it to change that setting.

The brightness target is the integer rounding of `100 × (1 − effective opacity)`. An inactive overlay therefore requests hardware brightness 100. This is a mapping chosen by the utility, not a measurement of screen luminance. It does not remember or restore a monitor's previous hardware setting.

Writes use `ddcutil setvcp 10 VALUE` with its default monitor selection. The utility does not discover a separate hardware target for every Qt screen. DDC/CI access depends on the monitor, cable, driver, and user permissions.

Changes are debounced for 150 ms. Only one `ddcutil` process runs at a time; when another target arrives during a write, the latest value is dispatched afterward. A write times out after five seconds. Failed writes do not count as successfully applied and can be retried by subsequent commands. Software dimming continues if hardware control fails.

## Named instances and environment

By default there is one daemon per user. A named instance creates a separate IPC namespace:

```bash
REDUCE_WHITE_INSTANCE=reading reduce-white --set 0.2
REDUCE_WHITE_INSTANCE=reading reduce-white --status
REDUCE_WHITE_INSTANCE=reading reduce-white --quit
```

On PowerShell:

```powershell
$env:REDUCE_WHITE_INSTANCE = 'reading'
reduce-white --set 0.2
reduce-white --quit
Remove-Item Env:REDUCE_WHITE_INSTANCE
```

Use the same instance value for all commands controlling that daemon. Instance names are hashed into socket/pipe names, so they cannot insert filesystem paths. Separate instances can create overlapping overlays; use them deliberately. The integration tests use this mechanism to isolate native pipe checks.

| Environment variable | Use |
| --- | --- |
| `REDUCE_WHITE_INSTANCE` | Optional independent daemon namespace. |
| `XDG_RUNTIME_DIR` | Unix IPC parent directory when absolute, private, and user-owned. |
| `DISPLAY` / `WAYLAND_DISPLAY` | Linux desktop connection, used by Qt. |
| `QT_QPA_PLATFORM` | Qt platform selection; `offscreen` is used by automated tests. |
| `QT_DEBUG_PLUGINS` | Qt plugin diagnostics during troubleshooting. |
| `CMAKE_PREFIX_PATH` | Qt development package discovery at configure time. |

The daemon inherits environment variables from the client that starts it. Run control commands from the intended graphical session. The same user in multiple desktop sessions normally shares one daemon unless separate instance names or runtime directories are used.

## Keyboard shortcuts and login startup

Assign command invocations such as `reduce-white --increase` and `reduce-white --decrease` to your desktop's global keyboard shortcuts. Use the installed command's full path when the shortcut manager does not inherit your shell PATH. A `--quit` shortcut provides a way to stop the overlay without navigating dimmed windows.

Auto-start on login is optional. Usually a login command such as `reduce-white --set 0.2` is sufficient because it starts a detached daemon. Configure it through the desktop's startup settings on Linux, a task running in the logged-in user session on Windows, or a user LaunchAgent on macOS. Start it after the graphical session is available, using an absolute installed path and the same instance environment as your shortcuts.

The installer does not create services, scheduled tasks, LaunchAgents, or system-wide startup entries. A system service without a logged-in GUI session is not equivalent to a user-session overlay.

## Platform behavior

### Linux X11

Qt creates frameless tool windows with transparent input, no focus, and a stays-on-top hint. Window managers determine stacking behavior. Protected/exclusive fullscreen surfaces may not be covered as expected. Verify mouse click-through and fullscreen interaction in your actual window manager.

### Linux Wayland

When LayerShellQt 6 is found during the build, the daemon configures overlay-layer surfaces anchored to all four screen edges, with no exclusive layout reservation and no keyboard interactivity. The compositor must implement the layer-shell protocol.

Without this integration, standard Qt windows are used. Wayland compositors can constrain window position and stacking, so that fallback cannot guarantee screen-wide dimming. The build log reports whether LayerShellQt was found. Qt 5 LayerShellQt packages do not satisfy the dependency.

### Windows

The daemon creates standard Qt transparent-input, non-focusable tool windows. IPC uses user-restricted named pipes and a lifetime lock. The command is a console executable so help/status/errors remain visible; detached daemon startup uses Qt's process API.

Check display scaling, screen hotplug, and normal fullscreen windows on a Windows desktop. Secure desktops, lock screens, UAC surfaces, and exclusive fullscreen rendering are outside the guarantee of a normal application overlay.

### macOS

The daemon uses a native app bundle and accessory activation policy. AppKit configures native overlay windows to ignore mouse events, remain visible on application deactivation, use a status-window level, and request participation in all Spaces plus fullscreen auxiliary behavior. Non-activating panel style is set when the native Qt window is an `NSPanel`.

These requests require validation against the target macOS release, fullscreen application, and Spaces configuration. Mission Control, protected system surfaces, and other windows at higher system levels may behave differently. No Screen Recording capture is used by this implementation.

The foreground daemon and detached daemon use the same overlay code. macOS executable discovery uses `_NSGetExecutablePath`, so auto-start does not depend on Linux `/proc` or the current working directory.
