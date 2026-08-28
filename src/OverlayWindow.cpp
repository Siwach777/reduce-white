#include "OverlayWindow.h"
#include <QPainter>
#include <QColor>
#include <QScreen>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QRegion>
#include <algorithm>
#include <cmath>

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

OverlayWindow::OverlayWindow(QScreen *screen, double opacity, QWindow *parent)
    : QRasterWindow(parent), m_opacityLevel(opacity) {
    
    Qt::WindowFlags winFlags = Qt::FramelessWindowHint | 
                               Qt::WindowTransparentForInput | 
                               Qt::WindowDoesNotAcceptFocus;
                            
    // Setup transparent drawing for QWindow
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    setFormat(format);
    
    // Set empty input mask to guarantee 100% click-through across all window managers
    setMask(QRegion());
    
    // Check if we are running under Wayland
    QString platform = QGuiApplication::platformName();
    bool isWayland = platform.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
    
#ifdef HAVE_LAYER_SHELL
    if (isWayland) {
        winFlags |= Qt::BypassWindowManagerHint;
        setFlags(winFlags);
        
        if (screen) {
            setScreen(screen);
            setGeometry(screen->geometry());
            connect(screen, &QScreen::geometryChanged, this, &OverlayWindow::onGeometryChanged);
        }
        
        // Configure layer-shell properties
        if (LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(this)) {
            layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
            LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorTop;
            anchors |= LayerShellQt::Window::AnchorBottom;
            anchors |= LayerShellQt::Window::AnchorLeft;
            anchors |= LayerShellQt::Window::AnchorRight;
            layerWindow->setAnchors(anchors);
            layerWindow->setExclusiveZone(-1); // Don't shift other windows
            layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        }
    } else {
#else
    {
#endif
        // Fallback for X11, Windows, macOS, or Wayland without LayerShellQt compiled in
        winFlags |= Qt::WindowStaysOnTopHint | Qt::Tool;
        setFlags(winFlags);
        
        if (screen) {
            setScreen(screen);
            setGeometry(screen->geometry());
            connect(screen, &QScreen::geometryChanged, this, &OverlayWindow::onGeometryChanged);
        }
    }
}

void OverlayWindow::onGeometryChanged(const QRect &geo) {
    setGeometry(geo);
    setMask(QRegion());
}

void OverlayWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int alpha = static_cast<int>(std::round(std::clamp(m_opacityLevel, 0.0, 1.0) * 255.0));
    painter.fillRect(QRect(0, 0, width(), height()), QColor(0, 0, 0, std::clamp(alpha, 0, 255)));
}

void OverlayWindow::setOpacityLevel(double opacity) {
    double newOpacity = std::clamp(opacity, 0.0, 1.0);
    // Fix: qFuzzyCompare fails at 0.0 due to relative epsilon; use absolute epsilon comparison
    if (std::abs(m_opacityLevel - newOpacity) < 0.0001) return;
    m_opacityLevel = newOpacity;
    update();
}

