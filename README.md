# Reduce White Point

A highly optimized, cross-platform utility to reduce the perceived brightness of your screens via a software overlay. It mimics the "Reduce White Point" feature from mobile OSes by applying a click-through, hardware-accelerated dark overlay. 

Built with extremely low resource consumption in mind, it operates silently in the background using minimal unshared memory and 0.0% idle CPU.

## Features
- **Cross-Platform**: Works natively on Linux (Wayland & X11) and Windows.
- **Wayland Native**: Uses `LayerShellQt` on Wayland (KDE Plasma, Sway, Hyprland, etc.) to guarantee the overlay correctly spans the screen and stays above all other windows.
- **Ultra-Lightweight**: Entirely stripped of heavy UI frameworks (no `QtWidgets`). It renders using low-level, hardware-accelerated raster surfaces and communicates via zero-allocation POSIX sockets.
- **Hardware Integration**: Can optionally interface with `ddcutil` on Linux to dim hardware brightness alongside the software overlay.
- **Single Instance & Daemonless Design**: Just run `reduce-white --set 0.3` anywhere. It auto-starts in the background in less than a millisecond if it isn't already running.

## Installation

We provide simple, one-click installation scripts. You will need CMake and Qt6 (`qt6-base` and `layer-shell-qt` on Linux) installed on your system.

### Linux (Arch / Ubuntu / Fedora / etc.)
```bash
# Make sure you have cmake and qt6 installed.
# e.g., on Arch: sudo pacman -S base-devel cmake qt6-base layer-shell-qt

./install.sh
```
*Note: This will build the project in Release mode and automatically copy it to `/usr/local/bin`.*

### Windows
```powershell
# Ensure CMake and Qt6 are installed (via vcpkg or Qt Maintenance Tool)
.\install.bat
```
*Note: After installation on Windows, ensure the destination directory is in your system PATH.*

### Universal / Python
```bash
python3 install.py
```

## Usage

**Important:** The command name is `reduce-white` (not `reduce-w`).

Control the dimming by running the command in your terminal or assigning it to a global keyboard shortcut in your Desktop Environment:

```bash
# Set opacity to 30% (auto-starts the background daemon if not running)
reduce-white --set 0.3

# Toggle the effect on/off
reduce-white --toggle

# Incrementally increase or decrease darkness (great for hotkeys!)
reduce-white --increase
reduce-white --decrease
```

### Hardware Brightness (`ddcutil`)
If you want the daemon to also lower hardware monitor brightness via your monitor's DDC/CI interface when you apply the filter:
1. Kill the background process if it is running: `killall reduce-white`
2. Start it manually once with the flag: `reduce-white --daemon --ddcutil &`
3. Subsequent commands (`reduce-white --increase`, etc.) will now control both software and hardware brightness.

## Architecture & Optimizations
- **Daemon Mode**: The primary process creates a `/tmp/reduce-white-ipc.sock` Unix Domain socket and sleeps. It uses 0% CPU unless a command is actively being received.
- **Client Mode**: When you run `reduce-white --increase`, it entirely bypasses Qt initialization, sends a rapid ASCII string over the POSIX socket to the daemon, and exits in <1ms. 
- **Memory Profiling**: While system monitors may report ~100MB RSS due to shared Qt Wayland libraries (which are likely already loaded into RAM by your desktop environment), the *Proportional Set Size* and unshared memory footprint is strictly constrained to a few megabytes. Memory allocations during IPC string parsing have been eliminated to prevent fragmentation over weeks of uptime.
