#include "Daemon.h"
#include "Command.h"
#include "IpcConfig.h"
#include <QDebug>
#include <QGuiApplication>
#include <QLocalSocket>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

Daemon::Daemon(bool useDdcutil, QObject *parent)
    : QObject(parent), m_useDdcutil(useDdcutil) {
    const auto path = QString::fromUtf8(getIpcSocketPath().c_str());
#ifdef _WIN32
    m_lock = std::make_unique<QLockFile>(QDir::tempPath() + '/' + path + ".lock");
#else
    m_lock = std::make_unique<QLockFile>(path + ".lock");
#endif
    // Do not steal a live daemon's lock just because it has been idle for 30 seconds.
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) throw std::runtime_error("Daemon is already running or IPC lock is unavailable.");
#ifndef _WIN32
    struct stat info {};
    const std::string socketPath = getIpcSocketPath();
    if (lstat(socketPath.c_str(), &info) == 0) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid()) {
            throw std::runtime_error("Refusing to replace an unexpected file at the IPC socket path.");
        }
    } else if (errno != ENOENT) {
        throw std::runtime_error("Cannot inspect the IPC socket path.");
    }
#endif
    // Only the lock owner may recover a socket left behind by a crashed daemon.
    QLocalServer::removeServer(path);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(32);
    if (!m_server.listen(path)) {
        throw std::runtime_error("Cannot listen on IPC socket: " + m_server.errorString().toStdString());
    }
    connect(&m_server, &QLocalServer::newConnection, this, &Daemon::handleConnection);

    if (m_useDdcutil) {
        m_ddcTimer.setSingleShot(true);
        m_ddcTimer.setInterval(150);
        connect(&m_ddcTimer, &QTimer::timeout, this, &Daemon::startHardwareUpdate);
        m_ddcTimeout.setSingleShot(true);
        m_ddcTimeout.setInterval(5000);
        connect(&m_ddcTimeout, &QTimer::timeout, &m_ddcProcess, &QProcess::kill);
        connect(&m_ddcProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            m_ddcTimeout.stop();
            if (code == 0 && status == QProcess::NormalExit) {
                m_lastHwBrightness = m_runningHwBrightness;
            } else {
                qWarning() << "ddcutil failed:" << m_ddcProcess.readAllStandardError().trimmed();
            }
            // A newer target must wait for the current I2C operation to finish.
            // Failed writes are retried on the next command, without a busy retry loop.
            if (m_pendingHwBrightness != m_runningHwBrightness) m_ddcTimer.start();
        });
        connect(&m_ddcProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_ddcTimeout.stop();
                qWarning() << "Cannot start ddcutil; check that it is installed.";
            }
        });
    }

    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app) {
        connect(app, &QGuiApplication::screenAdded, this, &Daemon::addScreen);
        connect(app, &QGuiApplication::screenRemoved, this, &Daemon::removeScreen);
        for (QScreen *screen : app->screens()) addScreen(screen);
    }
    updateOverlays();
}

Daemon::~Daemon() {
    m_server.close();
    qDeleteAll(m_overlays);
    if (m_ddcProcess.state() != QProcess::NotRunning) {
        m_ddcProcess.kill();
        m_ddcProcess.waitForFinished(1000);
    }
}

void Daemon::addScreen(QScreen *screen) {
    if (!screen || m_overlays.contains(screen)) return;
    const double opacity = m_isActive ? m_opacity : 0.0;
    auto *overlay = new OverlayWindow(screen, opacity);
    overlay->setVisible(opacity > 0.0);
    m_overlays.insert(screen, overlay);
}

void Daemon::removeScreen(QScreen *screen) {
    delete m_overlays.take(screen);
}

void Daemon::handleConnection() {
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_clients >= 32 || socket->state() == QLocalSocket::UnconnectedState) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++m_clients;
        socket->setReadBufferSize(257);
        auto *deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        deadline->start(2000);
        connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            --m_clients;
            socket->deleteLater();
        });
        const auto readCommand = [this, socket]() {
            if (socket->property("handled").toBool()) return;
            if (socket->bytesAvailable() > 256) {
                socket->setProperty("handled", true);
                socket->write("error: command too long\n");
                return;
            }
            if (!socket->canReadLine()) return;
            socket->setProperty("handled", true);
            const QByteArray bytes = socket->readLine();
            const auto command = trimCommand(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())));
            const QByteArray reply = processCommand(command);
            if (command == "quit") {
                m_server.close();
                connect(socket, &QLocalSocket::disconnected, qApp, &QCoreApplication::quit);
                QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
            }
            socket->write(reply);
            // Wait for the client to read its reply and close. On Windows,
            // disconnecting a named pipe here can discard unread reply bytes.
            // The connection deadline also bounds peers that never close.
        };
        connect(socket, &QLocalSocket::readyRead, socket, readCommand);
        readCommand(); // Data may already be buffered when the connection is accepted.
    }
}

QByteArray Daemon::processCommand(std::string_view cmd) {
    if (cmd == "ping") return "pong\n";
    if (cmd == "get" || cmd == "status") {
        return "opacity: " + QByteArray::number(m_opacity, 'f', 2) +
               " active: " + (m_isActive ? "1\n" : "0\n");
    }
    if (cmd == "quit") return "ok\n";
    if (cmd == "toggle") {
        m_isActive = !m_isActive;
    } else {
        const auto split = cmd.find(' ');
        const auto operation = cmd.substr(0, split);
        const bool hasArgument = split != std::string_view::npos;
        const auto argument = hasArgument ? trimCommand(cmd.substr(split + 1)) : std::string_view{};
        double value = 0.05;
        if (operation == "set") {
            if (!parseLevel(argument, value)) return "error: opacity must be a finite number from 0 to 1\n";
            m_opacity = value;
        } else if (operation == "increase" || operation == "decrease") {
            if (hasArgument && !parseLevel(argument, value, true)) {
                return "error: step must be a finite number greater than 0 and at most 1\n";
            }
            m_opacity = std::clamp(m_opacity + (operation == "increase" ? value : -value), 0.0, 1.0);
        } else {
            return "error: unknown command\n";
        }
        m_isActive = true;
    }
    updateOverlays();
    return "ok\n";
}

void Daemon::updateOverlays() {
    const double opacity = m_isActive ? m_opacity : 0.0;
    for (auto *overlay : std::as_const(m_overlays)) {
        overlay->setOpacityLevel(opacity);
        // Hidden overlays avoid transparent fullscreen composition when dimming is off.
        overlay->setVisible(opacity > 0.0);
    }
    if (m_useDdcutil) {
        m_pendingHwBrightness = static_cast<int>(std::round((1.0 - opacity) * 100.0));
        if (m_pendingHwBrightness != m_lastHwBrightness) m_ddcTimer.start();
    }
}

void Daemon::startHardwareUpdate() {
    if (m_ddcProcess.state() != QProcess::NotRunning || m_pendingHwBrightness == m_lastHwBrightness) return;
    m_runningHwBrightness = m_pendingHwBrightness;
    m_ddcProcess.setStandardOutputFile(QProcess::nullDevice());
    m_ddcProcess.start("ddcutil", {"setvcp", "10", QString::number(m_runningHwBrightness)});
    m_ddcTimeout.start();
}
