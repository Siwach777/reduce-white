# Reduce White Point

Reduce White Point dims every display with a translucent black overlay. Control it from a terminal or a desktop keyboard shortcut; a detached daemon starts automatically when needed. The overlay lets mouse input through and does not take keyboard focus.

The project uses C++17 and Qt 6 Core, Gui, and Network. It renders with `QRasterWindow`, without QtWidgets. Optional LayerShellQt integration handles compatible Wayland compositors, and optional Linux `ddcutil` integration controls hardware brightness.

## Quick start

Install the dependencies for your platform using the [installation guide](docs/installation.md), then run:

```bash
# Linux or macOS: build, test, and install into a user-local directory
./install.sh

# The same installer works on all supported platforms
python3 install.py
```

On Windows:

```powershell
.\install.bat --qt-prefix "C:\Qt\6.8.3\msvc2022_64"
```

The installer prints the `bin` directory to add to PATH. It runs regression checks before installation and does not request administrator privileges for the default installation.

```bash
reduce-white --set 0.3
reduce-white --decrease 0.1
reduce-white --toggle
reduce-white --status
reduce-white --quit
```

`--set 0.3` means 30% black overlay opacity. Increasing opacity makes the screen darker. Opacity 0 has no dimming effect; opacity 1 covers the screen with black.

## Platform support

| Platform | Implementation | Validation |
| --- | --- | --- |
| Linux X11 | Transparent, non-focusable, topmost Qt tool windows | Linux builds and isolated IPC tests verified locally; actual desktop behavior depends on the window manager. |
| Linux Wayland | LayerShellQt 6 overlay surfaces when available | Builds verified with and without LayerShellQt; the compositor must support layer-shell. |
| Windows | Qt named-pipe IPC, detached process startup, standard Qt overlay windows | Native x64 build, IPC stress checks, runtime deployment, relocation, and packaging passed CI. |
| macOS | Native executable discovery, Qt app bundle, AppKit click-through windows and Spaces behavior | Native Apple Silicon build, IPC stress checks, runtime deployment, relocation, and packaging passed CI. |

The [verified cross-platform CI run](https://github.com/Siwach777/reduce-white/actions/runs/36816783461) also passed the Linux LayerShellQt and numeric-parser fallback jobs. Automated tests use offscreen rendering; desktop stacking, click-through behavior, and monitor hotplugging still need the [manual desktop checks](docs/development.md#desktop-validation). Intel macOS and other CPU architectures need their own native validation.

Wayland compositors without layer-shell may reject fullscreen positioning or topmost stacking. macOS fullscreen Spaces, Windows exclusive fullscreen applications, secure desktops, and lock screens require separate visual checks. See [platform behavior](docs/usage.md#platform-behavior).

## Installation choices

| Choice | Command or setting | Result |
| --- | --- | --- |
| Default Linux installation | `./install.sh` | Installs into `~/.local`, using the system Qt runtime. |
| Default macOS installation | `./install.sh` | Installs a deployed app bundle and CLI wrapper into `~/Applications/ReduceWhite`. |
| Default Windows installation | `.\install.bat` | Installs the executable and Qt runtime into `%LOCALAPPDATA%\ReduceWhite`. |
| Custom prefix | `python3 install.py --prefix /path/to/install` | Installs into a directory you choose. |
| Portable runtime | `python3 install.py --portable --prefix ./dist` | Deploys Qt libraries/plugins alongside the application; requires Qt 6.5+. |
| Existing Qt runtime | `python3 install.py --system-qt` | Disables runtime deployment. |
| System-wide installation | `python3 install.py --system` | Installs into `/usr/local` on Unix or Program Files on Windows; privileges may be required. |

A portable distribution can be moved as a complete directory on a compatible OS and CPU architecture. It does not turn one binary into an application for every operating system or guarantee compatibility with older Linux libc versions. Downloadable CI packages and source-build instructions are covered in [installation](docs/installation.md#portable-packages).

## Features and behavior

- One overlay per actual screen, including screen additions/removals and geometry changes.
- Exact command validation and finite opacity/step values.
- One daemon per user and optional named instance, protected by a lifetime lock.
- Asynchronous IPC with bounded buffers, connection limits, and deadlines.
- Native Unix client dispatch without constructing a Qt application.
- Hidden overlays when dimming is off, and repaints only when the rendered alpha changes.
- Debounced, serialized, timeout-limited Linux hardware brightness writes.
- User-local installers, optional bundled runtime dependencies, and ZIP/TGZ packaging.

## Documentation

- [Installation and packaging](docs/installation.md): dependencies, platform setup, installers, relocation, upgrading, and removal.
- [Usage and platform behavior](docs/usage.md): every CLI command, hardware brightness, instances, shortcuts, startup, and platform limitations.
- [Architecture and development](docs/development.md): source layout, IPC protocol, resource management, build switches, CI, testing, and benchmarks.
- [Troubleshooting](docs/troubleshooting.md): build, runtime, compositor, IPC, hardware, and packaging failures.
- [Project review](docs/project_analysis_and_optimizations.md): verified bug fixes, performance changes, measurements, and outstanding validation.

## Manual build

Requirements: CMake 3.16+, a C++17 compiler, and Qt 6.2+ development packages. Runtime deployment requires Qt 6.5+. The numeric parser uses a Qt C-locale fallback on compilers without floating-point `from_chars`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$HOME/.local"
```

On macOS, the build output is `build/reduce-white.app/Contents/MacOS/reduce-white`. Installation also creates `bin/reduce-white`, a wrapper that locates the app relative to itself. On Windows, a multi-configuration build typically places the executable under `build/Release/`.

Stop a running daemon with `reduce-white --quit` before upgrading. The installer builds and installs files; it does not start the desktop overlay or configure login startup automatically.
