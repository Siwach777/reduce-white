#include "IpcConfig.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool deployed = argc == 3 && QString::fromLocal8Bit(argv[2]) == "--deployed";
    if (argc != 2 && !deployed) return 2;
    QTemporaryDir runtime(
#ifdef _WIN32
        QDir::tempPath() + "/rw-native-XXXXXX"
#else
        "/tmp/rw-native-XXXXXX"
#endif
    );
    if (!runtime.isValid()) return 1;
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    qputenv("REDUCE_WHITE_INSTANCE", runtime.path().toUtf8());
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_QPA_PLATFORM", "offscreen");
    if (deployed || qEnvironmentVariableIsSet("REDUCE_WHITE_TEST_DEPLOYED")) {
        for (const char *name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORMTHEME",
                                 "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"}) {
            env.remove(QString::fromLatin1(name));
        }
#ifdef _WIN32
        const auto system = env.value("SystemRoot", "C:/Windows");
        env.insert("PATH", system + "/System32;" + system);
#else
        env.insert("PATH", "/usr/bin:/bin");
#endif
    }
    QProcess daemon;
    daemon.setProcessEnvironment(env);
    const QString binary = QString::fromLocal8Bit(argv[1]);
    const auto path = QString::fromUtf8(getIpcSocketPath().c_str());
    struct Cleanup {
        QString path;
        ~Cleanup() {
            QLocalSocket socket;
            socket.connectToServer(path);
            if (socket.waitForConnected(100)) {
                socket.write("quit\n");
                socket.flush();
                socket.waitForReadyRead(1000);
            }
        }
    } cleanup{path};
    try {
        daemon.start(binary, {"--daemon"});
        if (!daemon.waitForStarted(3000)) throw std::runtime_error("Daemon process did not start");
        QElapsedTimer deadline;
        deadline.start();
        bool connected = false;
        while (deadline.elapsed() < 3000) {
            if (daemon.waitForFinished(0)) throw std::runtime_error("Daemon exited during startup");
            QLocalSocket socket;
            socket.connectToServer(path);
            if (socket.waitForConnected(100)) {
                socket.write("ping\n");
                socket.flush();
                if ((socket.bytesAvailable() || socket.waitForReadyRead(1000)) && socket.readAll() == "pong\n") {
                    connected = true;
                    break;
                }
            }
            QThread::msleep(10);
        }
        if (!connected) throw std::runtime_error("Daemon did not become responsive");
        const auto command = [&](const QStringList &args, int exitCode = 0) {
            QProcess client;
            client.setProcessEnvironment(env);
            client.start(binary, args);
            if (!client.waitForStarted(3000) || !client.waitForFinished(5000) ||
                client.exitStatus() != QProcess::NormalExit || client.exitCode() != exitCode) {
                throw std::runtime_error("Client failed: " + client.readAllStandardError().toStdString());
            }
            // Windows console streams use CRLF; the wire protocol still uses LF.
            return client.readAllStandardOutput().replace("\r\n", "\n");
        };
        command({"--set", "0.4"});
        if (command({"--get"}) != "opacity: 0.40 active: 1\n") throw std::runtime_error("Incorrect set/get state");
        command({"--set", "nan"}, 2);
        command({"--toggle"});
        if (command({"--get"}) != "opacity: 0.40 active: 0\n") throw std::runtime_error("Incorrect toggle state");
        command({"--increase", "0.1"});
        if (command({"--get"}) != "opacity: 0.50 active: 1\n") throw std::runtime_error("Incorrect increase state");
        command({"--daemon"}, 1);
        command({"--quit"});
        if (!daemon.waitForFinished(3000) || daemon.exitCode() != 0) throw std::runtime_error("Daemon did not shut down cleanly");
        // Use the same binary after moving/repackaging it; startup resolves its own path.
        command({"--set", "0.2"});
        if (command({"--get"}) != "opacity: 0.20 active: 1\n") throw std::runtime_error("Incorrect auto-start state");
        command({"--quit"});
        std::cout << "Native IPC smoke test passed\n";
    } catch (const std::exception &error) {
        daemon.waitForFinished(0);
        std::cerr << error.what() << '\n' << daemon.readAllStandardError().toStdString();
        if (daemon.state() != QProcess::NotRunning) {
            daemon.kill();
            daemon.waitForFinished(3000);
        }
        return 1;
    }
    return 0;
}
