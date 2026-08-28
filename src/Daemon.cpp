#include "Daemon.h"
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QProcess>
#include <QDebug>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

const char* SOCKET_PATH = "/tmp/reduce-white-ipc.sock";

Daemon::Daemon(bool useDdcutil, QObject *parent)
    : QObject(parent), m_notifier(nullptr), m_serverFd(-1), m_opacity(0.3), m_isActive(true), m_useDdcutil(useDdcutil), m_lastHwBrightness(-1) {
    
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
    if (m_serverFd >= 0) {
        close(m_serverFd);
        unlink(SOCKET_PATH);
    }
}

void Daemon::setupIpc() {
    m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFd < 0) return;

    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(m_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
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
    
    double initialOpacity = m_isActive ? m_opacity : 0.0;
    OverlayWindow *overlay = new OverlayWindow(screen, initialOpacity);
    overlay->show();
    overlay->raise();
    m_overlays.insert(screen->name(), overlay);
}

void Daemon::removeScreen(QScreen *screen) {
    if (!screen) return;
    
    QString name = screen->name();
    if (m_overlays.contains(name)) {
        OverlayWindow *overlay = m_overlays.take(name);
        if (overlay) {
            overlay->close();
            overlay->deleteLater();
        }
    }
}

void Daemon::handleConnection() {
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(m_serverFd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd >= 0) {
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRead = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead > 0) {
            QString cmd = QString::fromLatin1(buffer).trimmed();
            if (cmd != QLatin1String("ping")) {
                processCommand(cmd);
            }
        }
        close(client_fd);
    }
}

void Daemon::processCommand(const QString &cmd) {
    if (cmd.isEmpty()) return;
    
    QStringView view(cmd);
    
    if (view.startsWith(QLatin1String("set "))) {
        bool ok;
        double val = view.mid(4).toDouble(&ok);
        if (ok) {
            m_opacity = val;
            m_isActive = true;
        }
    } else if (view == QLatin1String("toggle")) {
        m_isActive = !m_isActive;
    } else if (view == QLatin1String("increase")) {
        m_opacity = std::min(1.0, m_opacity + 0.05);
        m_isActive = true;
    } else if (view == QLatin1String("decrease")) {
        m_opacity = std::max(0.0, m_opacity - 0.05);
        m_isActive = true;
    }
    
    updateOverlays();
}

void Daemon::updateOverlays() {
    double currentOpacity = m_isActive ? m_opacity : 0.0;
    
    for (OverlayWindow *overlay : std::as_const(m_overlays)) {
        if (overlay) {
            overlay->setOpacityLevel(currentOpacity);
        }
    }
    
    if (m_useDdcutil) {
        int hwBrightness = static_cast<int>((1.0 - currentOpacity) * 100);
        if (hwBrightness != m_lastHwBrightness) {
            QProcess::startDetached("ddcutil", {"setvcp", "10", QString::number(hwBrightness)});
            m_lastHwBrightness = hwBrightness;
        }
    }
}
