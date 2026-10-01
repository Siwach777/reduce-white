#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>
#include <string_view>
#include "OverlayWindow.h"

class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(bool useDdcutil = false, QObject *parent = nullptr);
    ~Daemon() override;

private:
    void addScreen(QScreen *screen);
    void removeScreen(QScreen *screen);
    void handleConnection();
    QByteArray processCommand(std::string_view cmd);
    void updateOverlays();
    void startHardwareUpdate();

    QMap<QScreen *, OverlayWindow *> m_overlays;
    // The lock outlives the server, including socket cleanup.
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
    QTimer m_ddcTimer;
    QTimer m_ddcTimeout;
    QProcess m_ddcProcess;
    double m_opacity = 0.3;
    bool m_isActive = true;
    bool m_useDdcutil;
    int m_lastHwBrightness = -1;
    int m_pendingHwBrightness = -1;
    int m_runningHwBrightness = -1;
    int m_clients = 0;
};
