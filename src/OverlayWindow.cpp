#include "OverlayWindow.h"
#include <QPainter>
#include <QColor>
#include <QScreen>
#include <QGuiApplication>
#include <QSurfaceFormat>

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
}

void OverlayWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int alpha = static_cast<int>(m_opacityLevel * 255.0);
    painter.fillRect(QRect(0, 0, width(), height()), QColor(0, 0, 0, alpha));
}

void OverlayWindow::setOpacityLevel(double opacity) {
    double newOpacity = std::max(0.0, std::min(1.0, opacity));
    if (qFuzzyCompare(m_opacityLevel, newOpacity)) return;
    m_opacityLevel = newOpacity;
    update();
}
