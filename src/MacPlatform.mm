#include "MacPlatform.h"
#include <QGuiApplication>
#include <QWindow>
#import <AppKit/AppKit.h>

void configureMacApplication() {
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }
}

void configureMacOverlay(QWindow *window) {
    // The offscreen test plugin does not expose an NSView.
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return;
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = [view window];
    if (!nativeWindow) return;
    if ([nativeWindow isKindOfClass:[NSPanel class]]) {
        [nativeWindow setStyleMask:[nativeWindow styleMask] | NSWindowStyleMaskNonactivatingPanel];
    }
    [nativeWindow setIgnoresMouseEvents:YES];
    [nativeWindow setHidesOnDeactivate:NO];
    [nativeWindow setLevel:NSStatusWindowLevel];
    [nativeWindow setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                      NSWindowCollectionBehaviorFullScreenAuxiliary];
}
