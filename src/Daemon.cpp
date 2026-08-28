#include "Daemon.h"
#include "IpcConfig.h"
#include <QGuiApplication>
#include <QScreen>
#include <QProcess>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

Daemon::Daemon(bool useDdcutil, QObject *parent)
    : QObject(parent)
    , m_overlays()
    , m_notifier(nullptr)
    , m_ddcTimer(nullptr)
    , m_serverFd(-1)
    , m_opacity(0.3)
    , m_isActive(true)
    , m_useDdcutil(useDdcutil)
    , m_lastHwBrightness(-1)
    , m_pendingHwBrightness(-1)
    , m_socketPath(getIpcSocketPath()) {
    
    if (m_useDdcutil) {
        m_ddcTimer = new QTimer(this);
        m_ddcTimer->setSingleShot(true);
        m_ddcTimer->setInterval(150); // 150ms debounce to prevent I2C bus lock contention
        connect(m_ddcTimer, &QTimer::timeout, this, [this]() {
            if (m_pendingHwBrightness != m_lastHwBrightness && m_pendingHwBrightness >= 0) {
                QProcess::startDetached("ddcutil", {"setvcp", "10", QString::number(m_pendingHwBrightness)});
                m_lastHwBrightness = m_pendingHwBrightness;
            }
        });
    }
    
    setupIpc();
    
    QGuiApplication *app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (app) {
        connect(app, &QGuiApplication::screenAdded, this, &Daemon::addScreen);
        connect(app, &QGuiApplication::screenRemoved, this, &Daemon::removeScreen);
        
        const auto screens = app->screens();
        for (QScreen *screen : screens) {
            addScreen(screen);
        }
    }
}

Daemon::~Daemon() {
    qDeleteAll(m_overlays);
    m_overlays.clear();
    if (m_serverFd >= 0) {
        close(m_serverFd);
        unlink(m_socketPath.c_str());
    }
}

void Daemon::setupIpc() {
    m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFd < 0) return;

    unlink(m_socketPath.c_str());

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(m_serverFd);
        m_serverFd = -1;
        return;
    }

    listen(m_serverFd, 5);

    // Make socket non-blocking
    int flags = fcntl(m_serverFd, F_GETFL, 0);
    fcntl(m_serverFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_serverFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Daemon::handleConnection);
}

void Daemon::addScreen(QScreen *screen) {
    if (!screen) return;
    
    QString name = screen->name();
    if (m_overlays.contains(name)) {
        OverlayWindow *old = m_overlays.take(name);
        if (old) {
            old->close();
            delete old;
        }
    }
    
    double initialOpacity = m_isActive ? m_opacity : 0.0;
    OverlayWindow *overlay = new OverlayWindow(screen, initialOpacity);
    overlay->show();
    overlay->raise();
    m_overlays.insert(name, overlay);
}

void Daemon::removeScreen(QScreen *screen) {
    if (!screen) return;
    
    QString name = screen->name();
    if (m_overlays.contains(name)) {
        OverlayWindow *overlay = m_overlays.take(name);
        if (overlay) {
            overlay->close();
            delete overlay;
        }
    }
}

void Daemon::handleConnection() {
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(m_serverFd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    
    if (client_fd >= 0) {
        char buffer[256];
        ssize_t bytesRead = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string_view rawCmd(buffer, static_cast<size_t>(bytesRead));
            
            // Fast zero-allocation whitespace trimming
            while (!rawCmd.empty() && (rawCmd.front() == ' ' || rawCmd.front() == '\r' || rawCmd.front() == '\n' || rawCmd.front() == '\t')) {
                rawCmd.remove_prefix(1);
            }
            while (!rawCmd.empty() && (rawCmd.back() == ' ' || rawCmd.back() == '\r' || rawCmd.back() == '\n' || rawCmd.back() == '\t')) {
                rawCmd.remove_suffix(1);
            }
            
            if (!rawCmd.empty()) {
                if (rawCmd == "ping") {
                    const char pong[] = "pong\n";
                    send(client_fd, pong, sizeof(pong) - 1, 0);
                } else {
                    processCommand(rawCmd, client_fd);
                }
            }
        }
        close(client_fd);
    }
}

void Daemon::processCommand(std::string_view cmd, int clientFd) {
    if (cmd.empty()) return;
    
    if (cmd.substr(0, 4) == "set ") {
        std::string_view valStr = cmd.substr(4);
        char *end = nullptr;
        double val = std::strtod(valStr.data(), &end);
        if (end != valStr.data()) {
            m_opacity = std::clamp(val, 0.0, 1.0);
            m_isActive = true;
            updateOverlays();
            const char ok[] = "ok\n";
            send(clientFd, ok, sizeof(ok) - 1, 0);
        }
    } else if (cmd == "toggle") {
        m_isActive = !m_isActive;
        updateOverlays();
        const char ok[] = "ok\n";
        send(clientFd, ok, sizeof(ok) - 1, 0);
    } else if (cmd.substr(0, 8) == "increase") {
        double step = 0.05;
        if (cmd.size() > 9 && cmd[8] == ' ') {
            std::string_view stepStr = cmd.substr(9);
            char *end = nullptr;
            double s = std::strtod(stepStr.data(), &end);
            if (end != stepStr.data() && s > 0.0) step = s;
        }
        m_opacity = std::clamp(m_opacity + step, 0.0, 1.0);
        m_isActive = true;
        updateOverlays();
        const char ok[] = "ok\n";
        send(clientFd, ok, sizeof(ok) - 1, 0);
    } else if (cmd.substr(0, 8) == "decrease") {
        double step = 0.05;
        if (cmd.size() > 9 && cmd[8] == ' ') {
            std::string_view stepStr = cmd.substr(9);
            char *end = nullptr;
            double s = std::strtod(stepStr.data(), &end);
            if (end != stepStr.data() && s > 0.0) step = s;
        }
        m_opacity = std::clamp(m_opacity - step, 0.0, 1.0);
        m_isActive = true;
        updateOverlays();
        const char ok[] = "ok\n";
        send(clientFd, ok, sizeof(ok) - 1, 0);
    } else if (cmd == "get" || cmd == "status") {
        char resp[128];
        int len = std::snprintf(resp, sizeof(resp), "opacity: %.2f active: %d\n", m_opacity, m_isActive ? 1 : 0);
        if (len > 0) {
            send(clientFd, resp, static_cast<size_t>(len), 0);
        }
    } else if (cmd == "quit") {
        const char ok[] = "ok\n";
        send(clientFd, ok, sizeof(ok) - 1, 0);
        QCoreApplication::quit();
    }
}

void Daemon::updateOverlays() {
    double currentOpacity = m_isActive ? m_opacity : 0.0;
    
    for (OverlayWindow *overlay : std::as_const(m_overlays)) {
        if (overlay) {
            overlay->setOpacityLevel(currentOpacity);
        }
    }
    
    if (m_useDdcutil && m_ddcTimer) {
        int hwBrightness = static_cast<int>(std::round((1.0 - currentOpacity) * 100.0));
        m_pendingHwBrightness = std::clamp(hwBrightness, 0, 100);
        m_ddcTimer->start(); // Trigger debounce timer
    }
}

