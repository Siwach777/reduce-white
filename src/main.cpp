#include <QGuiApplication>
#include <iostream>
#include <string>
#include <string_view>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "Daemon.h"
#include "IpcConfig.h"

static bool sendIpcCommand(std::string_view cmd, std::string *response = nullptr) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::string path = getIpcSocketPath();
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        send(sock, cmd.data(), cmd.size(), 0);
        
        char buffer[256];
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0 && response != nullptr) {
            buffer[n] = '\0';
            response->assign(buffer, static_cast<size_t>(n));
        }
        
        close(sock);
        return true;
    }
    close(sock);
    return false;
}

static std::string getExecutablePath(const char *argv0) {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return std::string(argv0);
}

static void spawnDaemonDetached(const std::string &execPath, bool useDdcutil) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) {
            _exit(0);
        }
        int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            if (devNull > STDERR_FILENO) close(devNull);
        }
        if (useDdcutil) {
            execl(execPath.c_str(), execPath.c_str(), "--daemon", "--ddcutil", nullptr);
        } else {
            execl(execPath.c_str(), execPath.c_str(), "--daemon", nullptr);
        }
        _exit(1);
    }
}

static void printHelp() {
    std::cout << "Reduce White Point Overlay v1.0\n\n"
              << "Usage: reduce-white [OPTIONS]\n\n"
              << "Options:\n"
              << "  -h, --help              Displays this help message.\n"
              << "  -v, --version           Displays version information.\n"
              << "  -d, --daemon            Run as background daemon (creates the overlay).\n"
              << "      --ddcutil           Enable ddcutil hardware brightness integration (daemon only).\n"
              << "      --set <opacity>     Set opacity level (0.0 to 1.0).\n"
              << "      --toggle            Toggle overlay on/off.\n"
              << "      --increase [step]   Increase opacity by step (default: 0.05).\n"
              << "      --decrease [step]   Decrease opacity by step (default: 0.05).\n"
              << "      --get, --status     Get current overlay opacity and active status.\n"
              << "      --quit              Terminate running background daemon.\n";
}

static void printVersion() {
    std::cout << "reduce-white version 1.0\n";
}

int main(int argc, char *argv[]) {
    bool isDaemonMode = false;
    bool useDdcutil = false;
    std::string clientCmd;
    bool expectResponse = false;

    // Fast-path zero-allocation argument inspection
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-d" || arg == "--daemon") {
            isDaemonMode = true;
        } else if (arg == "--ddcutil") {
            useDdcutil = true;
        } else if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        } else if (arg == "--set" && i + 1 < argc) {
            clientCmd = "set " + std::string(argv[++i]);
        } else if (arg.rfind("--set=", 0) == 0) {
            clientCmd = "set " + std::string(arg.substr(6));
        } else if (arg == "--toggle" || arg == "-t") {
            clientCmd = "toggle";
        } else if (arg == "--increase" || arg == "-i") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                clientCmd = "increase " + std::string(argv[++i]);
            } else {
                clientCmd = "increase";
            }
        } else if (arg == "--decrease") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                clientCmd = "decrease " + std::string(argv[++i]);
            } else {
                clientCmd = "decrease";
            }
        } else if (arg == "--get" || arg == "--status" || arg == "-g") {
            clientCmd = "get";
            expectResponse = true;
        } else if (arg == "--quit" || arg == "-q") {
            clientCmd = "quit";
        }
    }

    // Fast Daemon Mode execution
    if (isDaemonMode) {
        if (sendIpcCommand("ping")) {
            std::cerr << "Daemon is already running." << std::endl;
            return 1;
        }
        
        QGuiApplication app(argc, argv);
        app.setApplicationName("reduce-white");
        app.setApplicationVersion("1.0");
        
        Daemon daemon(useDdcutil);
        return app.exec();
    }

    // Fast Client Mode execution (<0.3ms latency, zero Qt initialization)
    if (!clientCmd.empty()) {
        std::string response;
        if (sendIpcCommand(clientCmd, expectResponse ? &response : nullptr)) {
            if (expectResponse && !response.empty()) {
                std::cout << response;
            }
            return 0;
        }

        if (clientCmd == "quit") {
            std::cout << "Daemon is not running." << std::endl;
            return 0;
        }

        // Daemon not running -> Auto-start daemon detached
        std::string exePath = getExecutablePath(argv[0]);
        spawnDaemonDetached(exePath, useDdcutil);

        bool connected = false;
        for (int i = 0; i < 30; ++i) {
            usleep(20000); // 20ms polling interval (max 600ms)
            if (sendIpcCommand(clientCmd, expectResponse ? &response : nullptr)) {
                connected = true;
                if (expectResponse && !response.empty()) {
                    std::cout << response;
                }
                break;
            }
        }

        if (!connected) {
            std::cerr << "Failed to auto-start and connect to daemon." << std::endl;
            return 1;
        }
        return 0;
    }

    printHelp();
    return 0;
}

