#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QSocketNotifier>
#include "OverlayWindow.h"

class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(bool useDdcutil = false, QObject *parent = nullptr);
    ~Daemon();

private slots:
    void addScreen(QScreen *screen);
    void removeScreen(QScreen *screen);
    void handleConnection();

private:
    void processCommand(const QString &cmd);
    void updateOverlays();
    void setupIpc();

    QMap<QString, OverlayWindow*> m_overlays;
    QSocketNotifier *m_notifier;
    int m_serverFd;
    double m_opacity;
    bool m_isActive;
    bool m_useDdcutil;
    int m_lastHwBrightness;
};
