#include "OverlayWindow.h"
#include <QPainter>
#include <QColor>
#include <QScreen>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <algorithm>
#include <cmath>
#ifdef __APPLE__
#include "MacPlatform.h"
#include <QPlatformSurfaceEvent>
#endif

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

OverlayWindow::OverlayWindow(QScreen *screen, double opacity, QWindow *parent)
    : QRasterWindow(parent), m_alpha(static_cast<int>(std::round(
          (std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 0.0) * 255.0))) {
    
    Qt::WindowFlags winFlags = Qt::FramelessWindowHint | 
                               Qt::WindowTransparentForInput | 
                               Qt::WindowDoesNotAcceptFocus;
                            
    // Setup transparent drawing for QWindow
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    setFormat(format);
    
    // Check if we are running under Wayland
    QString platform = QGuiApplication::platformName();
    [[maybe_unused]] bool isWayland = platform.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
    
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

bool OverlayWindow::event(QEvent *event) {
    const bool handled = QRasterWindow::event(event);
#ifdef __APPLE__
    if (event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
        configureMacOverlay(this);
    }
#endif
    return handled;
}

void OverlayWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(0, 0, width(), height()), QColor(0, 0, 0, m_alpha));
}

void OverlayWindow::setOpacityLevel(double opacity) {
    if (!std::isfinite(opacity)) return;
    const int alpha = static_cast<int>(std::round(std::clamp(opacity, 0.0, 1.0) * 255.0));
    if (m_alpha == alpha) return;
    m_alpha = alpha;
    update();
}
