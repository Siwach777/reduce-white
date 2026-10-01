# Installation and portable packaging

## Requirements

| Dependency | Minimum / purpose |
| --- | --- |
| CMake | 3.16+ for building. Newer CMake is recommended for runtime dependency deployment. |
| C++ compiler | C++17 support, matching the Qt kit's compiler and architecture. |
| Qt | 6.2+ Core, Gui, and Network development packages. Use Qt 6.5+ for bundled runtime deployment. |
| Python | Python 3 for the shared installer, regression script, and benchmark. Manual CMake builds do not require Python. |
| LayerShellQt | Optional version 6 on Linux Wayland. Qt 5 builds of LayerShellQt are intentionally ignored. |
| `ddcutil` | Optional Linux hardware brightness integration. |
| `patchelf` | Required only for portable Linux runtime deployment; adds/updates Qt plugin RPATHs. |

The project does not need QtWidgets, QtQuick, or a QML application runtime. Deployment tools can collect additional plugins installed with Qt; inspect package contents when producing a release.

## Linux dependencies

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-wayland layer-shell-qt python
```

The [Arch LayerShellQt package](https://archlinux.org/packages/extra/x86_64/layer-shell-qt/) supplies the Qt 6 integration. Install `ddcutil` separately if hardware brightness is desired.

### Ubuntu / Debian

For an X11 build, or a build using the standard Wayland window fallback:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-wayland python3
./install.sh --system-qt
```

LayerShellQt package versions differ by distribution release. Ubuntu 24.04's [`liblayershellqtinterface-dev`](https://packages.ubuntu.com/noble/liblayershellqtinterface-dev) depends on Qt 5 and cannot provide this project's Qt 6 integration. The build rejects that version instead of mixing Qt 5 and Qt 6. Use a distribution's Qt 6 LayerShellQt package, or build LayerShellQt 6 against the same Qt installation used for this project.

A system Qt older than 6.5 can build the utility, but cannot use the portable deployment option. Use `--system-qt` or configure a newer Qt installation explicitly.

### Fedora

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtwayland layer-shell-qt-devel python3
./install.sh
```

The Qt development and layer-shell packages are described by [Fedora's Qt package](https://packages.fedoraproject.org/pkgs/qt6-qtbase/qt6-qtbase-devel/) and [LayerShellQt package](https://packages.fedoraproject.org/pkgs/layer-shell-qt/layer-shell-qt-devel/).

Run the overlay inside the graphical session whose displays it should control. An SSH session or system service without the desktop environment variables will not create a visible overlay.

## macOS dependencies and setup

Install Xcode Command Line Tools and a Qt development installation. With Homebrew:

```bash
xcode-select --install
brew install cmake qtbase python
./install.sh --qt-prefix "$(brew --prefix qtbase)"
```

[Homebrew's `qtbase` formula](https://formulae.brew.sh/formula/qtbase) contains the Qt base modules used here. The installer also tries to discover the `qtbase` or `qt` formula automatically. For Qt installed through the Qt Maintenance Tool, pass that kit's installation prefix using `--qt-prefix`.

By default the installation layout is:

```text
~/Applications/ReduceWhite/
  reduce-white.app/
    Contents/
      MacOS/reduce-white
      Frameworks/...
      PlugIns/...
  bin/reduce-white
  share/doc/ReduceWhitePoint/...
```

The shell wrapper finds the application relative to its own installed location. Add its directory to PATH:

```bash
export PATH="$HOME/Applications/ReduceWhite/bin:$PATH"
reduce-white --set 0.2
reduce-white --quit
```

Put the PATH setting in your shell configuration if desired. The application bundle declares `LSUIElement`; the daemon also uses AppKit accessory activation policy to avoid a normal Dock application. The native window code requests mouse transparency and participation in Spaces/fullscreen auxiliary surfaces. Check those behaviors on the target macOS release using the [desktop validation checklist](development.md#desktop-validation).

A normal build targets the compiler's selected architecture. Apple Silicon and Intel binaries are different unless you deliberately build a universal application with a Qt kit that supplies both architectures:

```bash
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_PREFIX_PATH="/path/to/universal/Qt"
cmake --build build-universal --parallel
```

Do not combine a single-architecture Qt installation with a universal application target. The oldest macOS version supported by a package is constrained by both its Qt kit and deployment target.

## Windows dependencies and setup

Install Python 3, CMake, Visual Studio C++ build tools, and a matching Qt 6 desktop kit. For example, use the MSVC 2022 64-bit Qt kit with Visual Studio 2022 and an x64 target. Open a Developer PowerShell or other terminal configured for that compiler:

```powershell
.\install.bat --qt-prefix "C:\Qt\6.8.3\msvc2022_64"
```

The version/path is an example; substitute the kit actually installed on your machine. The default prefix is `%LOCALAPPDATA%\ReduceWhite`, with the command under `bin\reduce-white.exe`. Qt deployment adds runtime libraries, plugins, and `qt.conf` to the installation tree. Add `bin` to the user PATH or call the executable by its full path.

A manual multi-configuration build looks like:

```powershell
cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$env:LOCALAPPDATA/ReduceWhite"
```

Qt's [Windows deployment documentation](https://doc.qt.io/qt-6/windows-deployment.html) explains the runtime and compiler redistribution requirements for the chosen kit. A deployed Qt directory still needs the supported Windows version and corresponding compiler runtime.

## Shared installer options

The scripts resolve the source directory from their own location. You can invoke them while your terminal is in another directory:

```bash
python3 /path/to/reduce-white/install.py --prefix /path/to/install
```

| Option | Effect |
| --- | --- |
| No location option | User-local prefix: `~/.local`, `~/Applications/ReduceWhite`, or `%LOCALAPPDATA%\ReduceWhite`. |
| `--prefix PATH` | Install into the specified absolute or relative directory. Relative paths are resolved from the caller's working directory. |
| `--system` | Explicit system-wide installation. Unix uses `/usr/local` and `sudo` when necessary. Windows uses Program Files and expects an appropriately privileged terminal. |
| `--qt-prefix PATH` | Pass a Qt kit prefix as `CMAKE_PREFIX_PATH`. |
| `--build-dir PATH` | Keep generated build files in another directory. Useful when changing compilers, kits, or architectures. |
| `--portable` | Enable runtime deployment. Requires Qt 6.5+. |
| `--system-qt` | Disable runtime deployment; rely on the existing Qt installation. |
| `--skip-tests` | Build without the regression targets and skip CTest. |

`--prefix` and `--system` are mutually exclusive. `--portable` and `--system-qt` are mutually exclusive. The Bash entry point also accepts a positional prefix for compatibility: `./install.sh "$HOME/.local"`. The Windows entry point passes named options to the shared Python installer.

Install failures are reported once with a nonzero exit code. The installer does not retry a failed system installation as an unprivileged installation.

## Portable packages

Build and install a deployed runtime into a fresh directory:

On Linux, install `patchelf` first (`sudo apt install patchelf`, `sudo dnf install patchelf`, or `sudo pacman -S patchelf`). It is not needed for a system-Qt installation or for native Windows/macOS deployment.

```bash
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release \
  -DREDUCE_WHITE_DEPLOY_RUNTIME=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/dist"
cmake --build build-portable --config Release --parallel
ctest --test-dir build-portable -C Release --output-on-failure
cmake --install build-portable --config Release
```

Use a local prefix with the shared installer for the same operation:

```bash
python3 install.py --portable --prefix ./dist --build-dir ./build-portable
```

CMake's Qt deployment integration collects runtime dependencies and generates the appropriate relative Qt paths. Linux uses an executable RPATH relative to its `bin` directory; Windows receives `qt.conf`; macOS receives a deployed app bundle. Linux and Windows builds using Qt 6.10+ omit image codecs, icon engines, desktop themes, and other plugins unnecessary for the black raster overlay. The offscreen test plugin and available Wayland platform plugins are staged explicitly, including their runtime dependencies. Linux excludes libc and its dynamic loader from the archive and uses `patchelf` to make staged platform plugins relocatable.

Move the entire directory, preserving its internal structure. Moving only the executable loses its Qt libraries/plugins or macOS app bundle. Do not combine binaries and plugins from unrelated Qt versions.

To generate a distribution archive:

```bash
cpack --config build-portable/CPackConfig.cmake -C Release -B packages
```

The default archive is ZIP on Windows/macOS and TGZ on Linux, named with the project version, OS, and build processor. A build with runtime deployment disabled produces an archive that still depends on an installed Qt runtime. CPack does not make that build self-contained automatically.

Portability is limited to compatible systems. Linux still needs a compatible libc/loader, graphics stack, compositor, and architecture; glibc itself is intentionally not bundled. Windows and macOS packages require a compatible OS and architecture. Unsigned macOS bundles also need the normal release signing/notarization process before public distribution; this build does not claim to produce a notarized release.

The CI workflow installs, moves, smoke-tests, and packages native builds. Its checks should be reviewed on the actual platforms before advertising a supported release.

## Upgrade and remove

1. Stop the daemon with `reduce-white --quit` using the currently installed command.
2. Build/test/install the new version into the same prefix, or use a fresh directory for a portable distribution.
3. Start it with a control command and inspect `reduce-white --status`.

Older versions used different temporary socket locations and unframed commands. Stop an older daemon before upgrading so a live legacy process does not share the new server path.

CMake writes `install_manifest.txt` in the build directory with the installed files. For a dedicated portable prefix, remove that directory after stopping the daemon and removing its PATH/startup entries. For a shared prefix such as `~/.local` or `/usr/local`, remove only files listed in this project's installation manifest; never remove the whole shared prefix. A runtime deployment into a shared prefix may share libraries with other applications, so a dedicated directory is preferable for portable installations.
