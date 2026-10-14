#include <QWidget>
#include <QDebug>
#ifdef slots
#undef slots
#endif
#import <AppKit/AppKit.h>

void verifyNativeMini(QWidget* widget)
{
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    NSWindow* window = view.window;
    if ((window.styleMask & NSWindowStyleMaskTitled) || window.level < NSFloatingWindowLevel
        || window.hidesOnDeactivate || window.opaque) {
        qFatal("Native mini window must be borderless, floating, transparent, and remain visible while inactive");
    }
    qInfo() << "Native mini: borderless, floating, visible while inactive; level" << window.level;
}
