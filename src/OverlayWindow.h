#pragma once

#include <QRasterWindow>
#include <QScreen>

class OverlayWindow : public QRasterWindow {
    Q_OBJECT
public:
    explicit OverlayWindow(QScreen *screen, double opacity = 0.3, QWindow *parent = nullptr);
    void setOpacityLevel(double opacity);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onGeometryChanged(const QRect &geo);

private:
    double m_opacityLevel;
};
