# Troubleshooting

## CMake cannot find Qt

Install Qt development packages, not only runtime libraries. The project needs Core, Gui, and Network. For a separate Qt kit, pass its installation prefix:

```bash
python3 install.py --qt-prefix /path/to/Qt/kit
```

A Windows MSVC kit must match the selected compiler and architecture. A macOS universal build needs Qt libraries for both requested architectures. When changing kits/generators, select a fresh `--build-dir`; a CMake cache created by another compiler is not interchangeable.

## Portable deployment fails

Portable deployment requires Qt 6.5+. With an older distribution Qt, install using `--system-qt` or configure a newer kit.

Linux portable deployment also requires `patchelf`. Install it with the distribution's package manager before configuring a portable build. It can add an RPATH to plugins that have no existing entry, which CMake's built-in RPATH editor cannot always do. The configure error names the missing tool rather than producing an incomplete package silently.

If deployment cannot resolve a runtime dependency, inspect the install output and use a coherent Qt installation. A system installation can contain third-party plugins with extra dependencies. Qt 6.10+ builds exclude plugin types that this overlay does not use, reducing that dependency set. Staging into a new directory avoids leftovers from an earlier broader deployment.

Run the moved package as a complete directory. Keep `qt.conf`, plugins, runtime libraries, the macOS bundle, and CLI wrapper together. An archive built with deployment disabled still depends on installed Qt.

## Qt platform plugin is missing or incompatible

Typical messages mention `xcb`, `wayland`, `windows`, or `cocoa`. Enable diagnostics for a foreground daemon:

```bash
QT_DEBUG_PLUGINS=1 reduce-white --daemon
```

On PowerShell:

```powershell
$env:QT_DEBUG_PLUGINS = '1'
reduce-white --daemon
Remove-Item Env:QT_DEBUG_PLUGINS
```

Install the platform's Qt plugin/runtime packages or rebuild/deploy with the matching Qt kit. Check that global `QT_PLUGIN_PATH` or `QT_QPA_PLATFORM_PLUGIN_PATH` variables are not forcing plugins from another installation. An offscreen test succeeding does not prove a desktop platform plugin is installed.

## Auto-start fails or IPC times out

Run `reduce-white --daemon` in the foreground to reveal the startup error. Detached startup redirects diagnostic output to the null device.

Check that the command is running in the graphical user session, with the appropriate `DISPLAY`/`WAYLAND_DISPLAY` on Linux. Check `REDUCE_WHITE_INSTANCE` is consistent between commands. A daemon started with one namespace is independent of another namespace.

Unix runtime directories must be private and user-owned. An invalid/too-long runtime directory causes use of a private `/tmp/reduce-white-<UID>` directory. An existing unsafe fallback directory or unexpected file at the socket path causes a clear error; the application does not delete arbitrary files to recover.

A live lifetime lock prevents duplicate startup. Do not remove another running daemon's socket or lock. Use `--quit` to stop the current instance, and then start it again. A killed/crashed process can be recovered by a new lock owner automatically.

A connected timeout returns failure without replaying a toggle/increment. Check status before deciding whether to repeat a command.

## The command name is not found

The executable is `reduce-white`, with `.exe` on Windows. Add the installation's `bin` directory to PATH or invoke the full installed path. Shortcut managers and login startup systems may not inherit shell PATH changes.

Default directories:

| Platform | Command directory |
| --- | --- |
| Linux | `~/.local/bin` |
| macOS | `~/Applications/ReduceWhite/bin` |
| Windows | `%LOCALAPPDATA%\ReduceWhite\bin` |

The macOS installed command is a wrapper around the adjacent app bundle. Moving only that wrapper breaks its relative path to the bundle.

## Wayland coverage or stacking is wrong

Check the configure log for `Found LayerShellQt`. The project needs LayerShellQt 6, built against the same Qt kit. Ubuntu 24.04's Qt 5 LayerShellQt development package is not sufficient.

The compositor must implement layer-shell. Standard Wayland windows cannot force arbitrary placement/stacking on every compositor. Use a compatible compositor or validate the X11 session path when available. Rebuilding with layer-shell cannot add protocol support to a compositor that lacks it.

## Windows or macOS overlay behavior differs

Run the [desktop validation checklist](development.md#desktop-validation) on the native system. Automated IPC tests use an offscreen plugin and cannot verify click-through, focus, mixed scaling, fullscreen stacking, or Spaces behavior.

On macOS, use the installed wrapper or the executable inside `reduce-white.app/Contents/MacOS`. Ensure a package's architecture and deployment target match the machine. Review CI compile results for the Objective-C++ source before treating macOS support as verified.

On Windows, use the matching Qt/compiler runtime and test ordinary desktop fullscreen applications separately from exclusive fullscreen or secure system surfaces.

## Hardware brightness does not change

Hardware control is Linux-only and must be enabled when the daemon starts:

```bash
reduce-white --quit
reduce-white --daemon --ddcutil
```

Verify `ddcutil` is installed and can access your monitor's DDC/CI interface as the same user. Test its own commands independently. This utility uses its default display selection rather than matching every Qt screen to a DDC display.

Foreground daemon warnings distinguish a missing executable, failed write, or a killed timeout. Software overlays continue working even if hardware access fails. Writes are serialized, so a slow in-flight operation may delay a newer target until it completes or reaches the five-second limit.

## Tests fail in a restricted environment

The IPC suites create temporary sockets and processes. A sandbox denying socket creation produces permission/listen errors. LeakSanitizer can fail when process inspection is restricted. Run these checks in a suitable local/CI environment; do not treat permission failures as successful regression checks.

The Python suite skips raw Unix socket cases on Windows, while the native Qt smoke test exercises named pipes. CTest must be given `-C Release` when using a multi-configuration generator.

## Benchmark memory is unavailable

The Linux benchmark reads procfs, which may be restricted. It prints unavailable measurements instead of zero. It uses an offscreen daemon, so its memory/latency numbers do not predict every desktop compositor's resource use.
