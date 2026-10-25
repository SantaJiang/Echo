#import <AppKit/AppKit.h>
#include <QWindow>

void postNativeTitlebarDoubleClick(QWindow* window)
{
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    [native makeKeyAndOrderFront:nil];
    const NSPoint point = [view convertPoint:NSMakePoint(400, view.isFlipped ? 12 : view.bounds.size.height - 12) toView:nil];
    // Exercise the same native event monitor as a physical titlebar click.
    // QTest::mouseDClick sends Qt events directly and never reaches it.
    for (NSInteger count = 1; count <= 2; ++count) {
        for (NSEventType type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0
                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:native.windowNumber
                context:nil eventNumber:0 clickCount:count pressure:type == NSEventTypeLeftMouseDown ? 1 : 0];
            [NSApp postEvent:event atStart:NO];
        }
    }
}
