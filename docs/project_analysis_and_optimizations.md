# Project review and verified improvements

The review covers the C++ daemon/client/overlay, CMake configuration, installation scripts, benchmark, and documentation. The Linux test environment uses Qt 6.11.2 and GCC 16.2.1. Integration tests use private temporary runtime directories and the Qt offscreen platform.

## Correctness and reliability fixes

| Issue | Change |
| --- | --- |
| A client connecting without sending data blocked the daemon in `recv`. | The server now uses asynchronous `QLocalServer`/`QLocalSocket` connections with bounded buffers, connection counts, and deadlines. |
| Stream reads assumed a whole command arrived in one packet. | Commands are framed with a newline and processed after the complete frame arrives. |
| Clients ignored short sends, missing replies, and daemon errors. | The native Unix client handles partial I/O and interrupts, validates replies, and applies a one-second deadline. It suppresses SIGPIPE. |
| Concurrent startup unlinked a live daemon's socket. | A lifetime `QLockFile` guards server creation and stale socket cleanup. Startup failures propagate to the caller. |
| Shared temporary paths and unchecked runtime paths weakened user isolation. | Runtime directories are validated; the fallback is a private user directory. Unexpected files at the socket path are preserved. |
| `nan`, infinity, numeric prefixes with trailing junk, and unknown CLI options were accepted. | Client and server validate full numeric values and exact commands. Invalid CLI input exits before starting a daemon. |
| Several control flags silently overwrote earlier commands. | Conflicting commands and daemon/control combinations are rejected. |
| Different screens with the same name replaced each other's overlay. | Overlay ownership is keyed by `QScreen *`, and duplicate registration is ignored. |
| Closing or hiding the last overlay could end the daemon. | The application disables quit-on-last-window-close. |
| A debounce timer still allowed concurrent detached `ddcutil` processes. | An owned `QProcess` serializes writes, queues the latest value, checks completion, and limits execution time. |
| Windows source included unconditional POSIX APIs, and its GUI executable hid CLI output. | POSIX includes are guarded, Windows uses Qt IPC/process APIs, and the executable retains console output. |
| Immediate server disconnection could discard unread Windows named-pipe replies. | Connections stay open until the client reads and closes, with a deadline for uncooperative peers; native tests repeat status round trips. |
| macOS had no app bundle or native overlay handling. | The build creates an accessory app bundle and a relative CLI wrapper; AppKit configures click-through overlay windows and Spaces behavior. |
| Older C++ standard libraries lacked floating-point `from_chars`. | CMake probes the actual overload; a strict Qt C-locale parser provides the fallback, with a switch to test it explicitly. |
| Deployed Qt installations could miss platform plugins or keep development-machine paths. | Packaging explicitly includes required platform plugins and dependencies, adds relative paths, and checks a moved installation with Qt environment overrides removed. |
| Installers depended on the caller's directory and omitted the install configuration. | Scripts resolve their source location, build in parallel, select Release for installation, and accept a local prefix. |
| Python installer caught exceptions that its own helper had converted to `SystemExit`. | It now reports subprocess failures once and returns a failure code. |
| Benchmark selected arbitrary matching processes and restored a guessed opacity. | It now owns an isolated daemon and cleans up only that process, including on failures. |

## Performance improvements

- Cache the rendered 8-bit alpha and skip repaints when a new requested value produces the same pixels. This replaces an arbitrary floating-point epsilon that could both miss an alpha boundary and repaint within one alpha value.
- Hide overlays at zero effective opacity to avoid compositing transparent fullscreen windows.
- Preserve native Unix client dispatch without Qt application initialization.
- Keep hardware I2C operations serialized and debounced rather than spawning competing processes.
- Restrict optional LTO to release configurations; it can be disabled with `-DREDUCE_WHITE_ENABLE_IPO=OFF` for diagnostics.
- Separate benchmark process-launch costs from direct socket round-trip measurements, and distinguish RSS from proportional/private memory.

An empty window mask does not guarantee an empty input region. Input transparency relies on the Qt window flags rather than redundant `setMask(QRegion())` calls. Raster drawing is not itself a claim of GPU rendering, and measured idle CPU or latency is not a guarantee for every desktop.

## Validation

Run `ctest --test-dir build --output-on-failure` after building. The regression suite covers numeric validation, state transitions, stalled/disconnected clients, fragmented/oversized requests, concurrent clients and auto-start, duplicate daemons, crash recovery, socket/lock cleanup, unexpected file preservation, serialized hardware writes, and installer failures/configuration.

Release builds with and without LayerShellQt, the forced Qt numeric parser, and a Debug build with AddressSanitizer/UndefinedBehaviorSanitizer passed the native IPC smoke test and the 19-test Python regression suite. The user-local installer also completed its configure/build/test/install sequence. Sanitizers require an environment that permits process inspection; socket tests require permission to create local sockets.

A bundled Linux installation and the extracted CPack TGZ archive passed the native smoke test after relocation with Qt plugin paths and library environment overrides removed. That check exercises daemon startup, named-instance IPC, set/get/toggle/increase, invalid input, duplicate-daemon protection, clean shutdown, and detached automatic startup using the relocated binary. Native CI repeats build, tests, installation, relocation, and archive generation on Linux, Windows, and macOS; separate Linux jobs cover LayerShellQt and the fallback parser.

A 1,000-request offscreen benchmark recorded 7.637 ms per process launch plus IPC request and 0.022 ms per direct socket request. Private memory changed from 5.29 to 5.30 MiB, and PSS from 6.48 to 6.49 MiB. These are measurements from one Linux run, not desktop performance guarantees or proof of leak freedom.

The [native CI run for commit `a0eafbe`](https://github.com/Siwach777/reduce-white/actions/runs/36816783461) passed all five jobs: Linux x64 and Windows x64 with Qt 6.8.3, macOS Apple Silicon with Qt 6.10.3, Fedora's Qt/LayerShellQt build, and Ubuntu's forced Qt numeric parser. The native jobs built, tested, installed bundled runtimes, moved the installations, exercised the moved executable with development paths removed, and generated downloadable archives.

The [expanded CI run for commit `0043672`](https://github.com/Siwach777/reduce-white/actions/runs/36871611039) passed all six jobs, adding a native Intel macOS build. Every native job also extracted its CPack archive, preserved executable permissions, moved it into a path containing spaces and Unicode, and repeated the isolated IPC/auto-start checks. The native test now obtains executable paths through Qt's Unicode command-line arguments on Windows. Linux CI uses an explicit Ubuntu 24.04 baseline; the checkout, Python setup, and artifact upload actions use their Node 24 versions.

Actual monitor hotplugging, compositor stacking/click-through behavior, and real DDC/CI hardware still need desktop validation. The hardware regression uses a controlled subprocess shim. Other CPU architectures remain untested.

Qt's [local server](https://doc.qt.io/qt-6/qlocalserver.html) and [local socket](https://doc.qt.io/qt-6/qlocalsocket.html) documentation describe the IPC access flags and asynchronous lifecycle used here.
