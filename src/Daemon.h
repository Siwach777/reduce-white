#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QSocketNotifier>
#include <QTimer>
#include <string>
#include <string_view>
#include "OverlayWindow.h"

class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(bool useDdcutil = false, QObject *parent = nullptr);
    ~Daemon() override;

private slots:
    void addScreen(QScreen *screen);
    void removeScreen(QScreen *screen);
    void handleConnection();

private:
    void processCommand(std::string_view cmd, int clientFd);
    void updateOverlays();
    void setupIpc();

    QMap<QString, OverlayWindow*> m_overlays;
    QSocketNotifier *m_notifier;
    QTimer *m_ddcTimer;
    int m_serverFd;
    double m_opacity;
    bool m_isActive;
    bool m_useDdcutil;
    int m_lastHwBrightness;
    int m_pendingHwBrightness;
    std::string m_socketPath;
};

