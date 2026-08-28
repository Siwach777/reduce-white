#include <QGuiApplication>
#include <QCommandLineParser>
#include <QProcess>
#include <QThread>
#include <iostream>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "Daemon.h"

extern const char* SOCKET_PATH;

bool sendCommand(const QString &cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        QByteArray data = cmd.toLatin1();
        send(sock, data.constData(), data.size(), 0);
        close(sock);
        return true;
    }
    close(sock);
    return false;
}

int main(int argc, char *argv[]) {
    // Optimization: Only initialize heavy QGuiApplication if running as daemon.
    bool isDaemonMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-d" || arg == "--daemon") {
            isDaemonMode = true;
            break;
        }
    }

    QCoreApplication *app;
    if (isDaemonMode) {
        app = new QGuiApplication(argc, argv);
    } else {
        app = new QCoreApplication(argc, argv);
    }
    
    app->setApplicationName("reduce-white");
    app->setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Reduce White Point Overlay");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption daemonOption(QStringList() << "d" << "daemon", "Run as daemon (creates the overlay)");
    parser.addOption(daemonOption);

    QCommandLineOption ddcutilOption("ddcutil", "Enable ddcutil hardware brightness integration (daemon only)");
    parser.addOption(ddcutilOption);

    QCommandLineOption setOption("set", "Set opacity level (0.0 to 1.0)", "opacity");
    parser.addOption(setOption);

    QCommandLineOption toggleOption("toggle", "Toggle overlay on/off");
    parser.addOption(toggleOption);

    QCommandLineOption increaseOption("increase", "Increase opacity by 5%");
    parser.addOption(increaseOption);

    QCommandLineOption decreaseOption("decrease", "Decrease opacity by 5%");
    parser.addOption(decreaseOption);

    parser.process(*app);

    if (parser.isSet(daemonOption)) {
        if (sendCommand("ping")) {
            std::cerr << "Daemon is already running." << std::endl;
            delete app;
            return 1;
        }
        Daemon daemon(parser.isSet(ddcutilOption));
        int ret = app->exec();
        delete app;
        return ret;
    } else {
        QString cmd;
        if (parser.isSet(setOption)) {
            cmd = "set " + parser.value(setOption);
        } else if (parser.isSet(toggleOption)) {
            cmd = "toggle";
        } else if (parser.isSet(increaseOption)) {
            cmd = "increase";
        } else if (parser.isSet(decreaseOption)) {
            cmd = "decrease";
        }

        if (!cmd.isEmpty()) {
            if (!sendCommand(cmd)) {
                QProcess::startDetached(app->applicationFilePath(), QStringList() << "--daemon");
                bool connected = false;
                for (int i = 0; i < 20; ++i) {
                    QThread::msleep(100);
                    if (sendCommand(cmd)) {
                        connected = true;
                        break;
                    }
                }
                if (!connected) {
                    std::cerr << "Failed to auto-start and connect to daemon." << std::endl;
                    delete app;
                    return 1;
                }
            }
            delete app;
            return 0;
        } else {
            parser.showHelp();
            delete app;
            return 0;
        }
    }
}
