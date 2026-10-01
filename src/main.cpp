#include "Command.h"
#include "Daemon.h"
#include "IpcConfig.h"
#include <QGuiApplication>
#include <QProcess>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#ifdef __APPLE__
#include "MacPlatform.h"
#include <mach-o/dyld.h>
#include <vector>
#endif

#ifdef _WIN32
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifdef HAVE_LAYER_SHELL
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#include <LayerShellQt/Shell>
#endif
#endif

enum class IpcResult { Success, Unavailable, Error };

static IpcResult sendIpcCommand(std::string_view cmd, std::string &response) {
    response.clear();
    const std::string path = getIpcSocketPath();
    const std::string request = std::string(cmd) + '\n';
#ifdef _WIN32
    QLocalSocket socket;
    QElapsedTimer deadline;
    deadline.start();
    const auto remaining = [&deadline]() { return std::max(0, 1000 - static_cast<int>(deadline.elapsed())); };
    socket.connectToServer(QString::fromUtf8(path.c_str()));
    if (!socket.waitForConnected(remaining())) {
        return socket.error() == QLocalSocket::ServerNotFoundError ||
                       socket.error() == QLocalSocket::ConnectionRefusedError
                   ? IpcResult::Unavailable : IpcResult::Error;
    }
    if (socket.write(request.data(), static_cast<qint64>(request.size())) != static_cast<qint64>(request.size())) {
        return IpcResult::Error;
    }
    while (socket.bytesToWrite() > 0) {
        if (remaining() == 0 || !socket.waitForBytesWritten(remaining())) return IpcResult::Error;
    }
    while (response.find('\n') == std::string::npos && response.size() < 256) {
        if (!socket.bytesAvailable() && (remaining() == 0 || !socket.waitForReadyRead(remaining()))) {
            return IpcResult::Error;
        }
        const auto bytes = socket.read(256 - static_cast<qint64>(response.size()));
        response.append(bytes.constData(), static_cast<size_t>(bytes.size()));
    }
#else
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return IpcResult::Error;
    struct SocketGuard {
        int fd;
        ~SocketGuard() { close(fd); }
    } guard{fd};
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) return IpcResult::Error;
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
#ifdef __APPLE__
    addr.sun_len = sizeof(addr);
#endif
    if (path.size() >= sizeof(addr.sun_path)) return IpcResult::Error;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto waitFor = [fd, deadline](short events) {
        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return false;
            pollfd descriptor{fd, events, 0};
            const int result = poll(&descriptor, 1, static_cast<int>(remaining));
            if (result < 0 && errno == EINTR) continue;
            return result > 0 && (descriptor.revents & events) != 0;
        }
    };
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) return IpcResult::Unavailable;
        if (errno != EINPROGRESS || !waitFor(POLLOUT)) return IpcResult::Error;
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error != 0) return IpcResult::Error;
    }
    size_t sent = 0;
    while (sent < request.size()) {
        if (!waitFor(POLLOUT)) return IpcResult::Error;
        const auto count = send(fd, request.data() + sent, request.size() - sent, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return IpcResult::Error;
        sent += static_cast<size_t>(count);
    }
    while (response.find('\n') == std::string::npos && response.size() < 256) {
        if (!waitFor(POLLIN)) return IpcResult::Error;
        char buffer[256];
        const auto count = recv(fd, buffer, sizeof(buffer) - response.size(), 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return IpcResult::Error;
        response.append(buffer, static_cast<size_t>(count));
    }
#endif
    if (response == "ok\n" || response == "pong\n" || response.rfind("opacity: ", 0) == 0) {
        return response.back() == '\n' ? IpcResult::Success : IpcResult::Error;
    }
    return IpcResult::Error;
}

static void printHelp() {
    std::cout << "Reduce White Point Overlay v1.0\n\n"
              << "Usage: reduce-white [OPTIONS]\n\n"
              << "  -h, --help              Display this help.\n"
              << "  -v, --version           Display version information.\n"
              << "  -d, --daemon            Run the overlay daemon in the foreground.\n"
              << "      --ddcutil           Enable Linux hardware brightness integration on startup.\n"
              << "      --set <opacity>     Set opacity (0 to 1).\n"
              << "  -t, --toggle            Toggle the overlay on/off.\n"
              << "  -i, --increase [step]   Increase opacity (default step: 0.05).\n"
              << "      --decrease [step]   Decrease opacity (default step: 0.05).\n"
              << "  -g, --get, --status     Show opacity and active status.\n"
              << "  -q, --quit              Stop the daemon.\n";
}

static int run(int argc, char *argv[]) {
    bool daemonMode = false;
    bool useDdcutil = false;
    std::string command;
    const auto fail = [](std::string_view message) {
        std::cerr << "Error: " << message << '\n';
        return 2;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") { printHelp(); return 0; }
        if (arg == "--version" || arg == "-v") { std::cout << "reduce-white version 1.0\n"; return 0; }
        if (arg == "--daemon" || arg == "-d") {
            daemonMode = true;
            continue;
        }
        if (arg == "--ddcutil") {
#ifdef __linux__
            useDdcutil = true;
            continue;
#else
            return fail("--ddcutil is supported only on Linux.");
#endif
        }
        if (!command.empty()) return fail("Use one control command per invocation.");
        if (arg == "--set" || arg.rfind("--set=", 0) == 0) {
            std::string_view value;
            if (arg == "--set") {
                if (++i == argc) return fail("--set requires an opacity.");
                value = argv[i];
            } else {
                value = arg.substr(6);
            }
            double level = 0.0;
            if (!parseLevel(value, level)) return fail("Opacity must be a finite number from 0 to 1.");
            command = "set " + std::string(value);
        } else if (arg == "--increase" || arg == "-i" || arg == "--decrease") {
            command = arg == "--decrease" ? "decrease" : "increase";
            if (i + 1 < argc) {
                const std::string_view next(argv[i + 1]);
                const bool option = next.rfind("--", 0) == 0 || next == "-d" || next == "-g" ||
                                    next == "-h" || next == "-i" || next == "-q" || next == "-t" || next == "-v";
                if (!option) {
                    double step = 0.0;
                    if (!parseLevel(next, step, true)) return fail("Step must be greater than 0 and at most 1.");
                    command += " " + std::string(argv[++i]);
                }
            }
        } else if (arg == "--toggle" || arg == "-t") {
            command = "toggle";
        } else if (arg == "--get" || arg == "--status" || arg == "-g") {
            command = "get";
        } else if (arg == "--quit" || arg == "-q") {
            command = "quit";
        } else {
            return fail("Unknown option: " + std::string(arg));
        }
    }
    if (daemonMode && !command.empty()) return fail("--daemon cannot be combined with a control command.");
    if (command.size() > 255) return fail("Command is too long.");
    if (useDdcutil && !daemonMode && command.empty()) return fail("--ddcutil requires --daemon or a control command.");
    if (!daemonMode && command.empty()) { printHelp(); return 0; }
    if (daemonMode) {
#ifdef HAVE_LAYER_SHELL
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        LayerShellQt::Shell::useLayerShell();
#endif
#endif
        QGuiApplication app(argc, argv);
        app.setApplicationName("reduce-white");
        app.setApplicationVersion("1.0");
        app.setQuitOnLastWindowClosed(false);
#ifdef __APPLE__
        configureMacApplication();
#endif
        Daemon daemon(useDdcutil);
        return app.exec();
    }
#ifdef _WIN32
    // Windows named pipes use Qt; Unix clients keep the native fast path.
    QCoreApplication app(argc, argv);
#endif
    std::string response;
    IpcResult result = sendIpcCommand(command, response);
    if (result == IpcResult::Unavailable) {
        if (command == "quit") { std::cout << "Daemon is not running.\n"; return 0; }
        QString executable;
#ifdef _WIN32
        executable = QCoreApplication::applicationFilePath();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> path(size);
        if (_NSGetExecutablePath(path.data(), &size) != 0) {
            std::cerr << "Cannot resolve the macOS executable path.\n";
            return 1;
        }
        executable = QString::fromLocal8Bit(path.data());
#else
        char path[4096];
        const auto length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length > 0) {
            path[length] = '\0';
            executable = QString::fromLocal8Bit(path);
        } else {
            executable = QString::fromLocal8Bit(argv[0]);
        }
#endif
        QProcess process;
        process.setProgram(executable);
        QStringList arguments{"--daemon"};
        if (useDdcutil) arguments.append("--ddcutil");
        process.setArguments(arguments);
        process.setStandardInputFile(QProcess::nullDevice());
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        if (!process.startDetached()) {
            std::cerr << "Failed to start daemon: " << process.errorString().toStdString() << '\n';
            return 1;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            result = sendIpcCommand(command, response);
        } while (result == IpcResult::Unavailable && std::chrono::steady_clock::now() < deadline);
    }
    // Never retry after a connected request fails: a toggle may already have applied.
    if (result != IpcResult::Success) {
        std::cerr << (response.empty() ? "Cannot communicate with daemon (startup failed or IPC timed out).\n" : response);
        return 1;
    }
    if (command == "get") std::cout << response;
    return 0;
}

int main(int argc, char *argv[]) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
