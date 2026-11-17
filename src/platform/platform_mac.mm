#include "platform/platform.h"

#include <QImage>
#include <QGuiApplication>
#include <QPointer>
#include <QLoggingCategory>
#include <QTimer>
#include <QElapsedTimer>
#include <QWindow>

#include <dispatch/dispatch.h>
#include <cmath>

#ifdef slots
#undef slots
#endif
#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

// Handle status-item mouse events directly. macOS 27 can deliver NSButton's
// action after currentEvent has become a gesture event, losing left/right intent.
@interface EchoStatusHandler : NSObject {
@public
    std::function<void()> openWindow;
    std::function<void()> openSettings;
}
- (void)open:(id)sender;
- (void)settings:(id)sender;
- (void)quit:(id)sender;
@end

@implementation EchoStatusHandler
- (void)open:(id)sender { Q_UNUSED(sender); [NSApp activateIgnoringOtherApps:YES]; openWindow(); }
- (void)settings:(id)sender { Q_UNUSED(sender); [NSApp activateIgnoringOtherApps:YES]; openSettings(); }
- (void)quit:(id)sender { Q_UNUSED(sender); echo::quitApplication(); }
@end

@interface EchoMenuAction : NSObject {
@public
    std::function<void()> callback;
}
- (void)invoke:(id)sender;
@end
@implementation EchoMenuAction
- (void)invoke:(id)sender { Q_UNUSED(sender); if (callback) callback(); }
@end

namespace echo {
namespace {

Q_LOGGING_CATEGORY(windowLog, "echo.window", QtWarningMsg)

std::function<void(MediaCommand, qint64)> g_handler;

void dispatchCommand(MediaCommand command, qint64 argument)
{
    auto handler = g_handler;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (handler) {
            handler(command, argument);
        }
    });
}

} // namespace

void installTrayIcon(QObject* owner, const std::function<void()>& open, const std::function<void()>& settings,
    const std::function<void(const QPoint&)>& context)
{
    Q_UNUSED(context);
    NSStatusItem* item = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    // A template image lets macOS choose the correct menu-bar contrast.
    NSImage* image = [NSImage imageWithSize:NSMakeSize(20, 20) flipped:NO drawingHandler:^BOOL(NSRect) {
        [NSColor.blackColor setStroke];
        NSBezierPath* record = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(1, 1, 18, 18)];
        record.lineWidth = 1.2;
        [record stroke];
        NSBezierPath* groove = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(3, 3, 14, 14)];
        groove.lineWidth = 0.65;
        [groove stroke];
        NSBezierPath* shine = [NSBezierPath bezierPath];
        shine.lineWidth = 1.15;
        shine.lineCapStyle = NSLineCapStyleRound;
        [shine appendBezierPathWithArcWithCenter:NSMakePoint(10, 10) radius:8 startAngle:95 endAngle:145];
        [shine moveToPoint:NSMakePoint(10 + 8 * cos(275 * M_PI / 180), 10 + 8 * sin(275 * M_PI / 180))];
        [shine appendBezierPathWithArcWithCenter:NSMakePoint(10, 10) radius:8 startAngle:275 endAngle:325];
        [shine stroke];
        [NSColor.blackColor setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(5.2, 5.2, 9.6, 9.6)] fill];
        // Knock the E out of the label so template contrast remains automatic.
        CGContextRef context = NSGraphicsContext.currentContext.CGContext;
        CGContextSaveGState(context);
        CGContextSetBlendMode(context, kCGBlendModeClear);
        CGContextSetLineWidth(context, 1.1);
        CGContextSetLineCap(context, kCGLineCapRound);
        CGContextSetLineJoin(context, kCGLineJoinRound);
        CGContextMoveToPoint(context, 11.7, 12.5);
        CGContextAddLineToPoint(context, 8.3, 12.5);
        CGContextAddLineToPoint(context, 8.3, 7.5);
        CGContextAddLineToPoint(context, 11.7, 7.5);
        CGContextMoveToPoint(context, 8.3, 10);
        CGContextAddLineToPoint(context, 11, 10);
        CGContextStrokePath(context);
        CGContextRestoreGState(context);
        return YES;
    }];
    image.accessibilityDescription = @"Echo";
    [image setTemplate:YES];
    item.button.image = image;
    item.button.toolTip = @"Echo";
    [item.button setAccessibilityLabel:@"Echo"];
    EchoStatusHandler* handler = [EchoStatusHandler new];
    handler->openWindow = open;
    handler->openSettings = settings;
    item.button.target = handler;
    item.button.action = @selector(open:); // keyboard / accessibility activation
    NSMenu* menu = [NSMenu new];
    for (NSArray* spec in @[@[@"打开 Echo", @"open:"], @[@"设置…", @"settings:"], @[@"退出 Echo", @"quit:"]]) {
        if ([spec[1] isEqual:@"quit:"]) [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:spec[0] action:NSSelectorFromString(spec[1]) keyEquivalent:@""];
        entry.target = handler;
        [menu addItem:entry];
    }
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown
        handler:^NSEvent*(NSEvent* event) {
            if (!item.button.window || event.window != item.button.window) return event;
            if (event.type == NSEventTypeRightMouseDown || (event.modifierFlags & NSEventModifierFlagControl)) {
                [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, item.button.bounds.size.height) inView:item.button];
            } else {
                [handler open:nil];
            }
            return nil;
        }];
    QTimer::singleShot(500, owner, [item] {
        const NSRect frame = item.button.window.frame;
        qCDebug(windowLog) << "Menu bar icon frame (native)" << frame.origin.x << frame.origin.y << frame.size.width << frame.size.height;
    });
    QObject::connect(owner, &QObject::destroyed, [item, monitor, handler, menu] {
        Q_UNUSED(handler); Q_UNUSED(menu);
        [NSEvent removeMonitor:monitor];
        [NSStatusBar.systemStatusBar removeStatusItem:item];
    });
}

void installApplicationMenu(QObject* owner, const QVariantList& menus, const std::function<void(int)>& invoke)
{
    if (QGuiApplication::platformName() != "cocoa") return;
    const QPointer<QObject> guarded(owner);
    NSMutableArray* targets = [NSMutableArray new];
    std::function<NSMenu*(const QVariantList&)> build = [&](const QVariantList& entries) -> NSMenu* {
        NSMenu* menu = [NSMenu new];
        for (const auto& value : entries) {
            const auto entry = value.toMap();
            if (entry.value("separator").toBool()) { [menu addItem:NSMenuItem.separatorItem]; continue; }
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:entry.value("text").toString().toNSString() action:nil keyEquivalent:@""];
            if (entry.contains("children")) item.submenu = build(entry.value("children").toList());
            else {
                EchoMenuAction* action = [EchoMenuAction new]; const int token = entry.value("token").toInt();
                action->callback = [guarded, invoke, token] { if (guarded) invoke(token); };
                item.target = action; item.action = @selector(invoke:); [targets addObject:action];
            }
            [menu addItem:item];
        }
        return menu;
    };
    NSMenu* main = build(menus);
    NSMenuItem* appEntry = [NSMenuItem new];
    NSMenu* app = [[NSMenu alloc] initWithTitle:@"Echo"];
    EchoMenuAction* quitAction = [EchoMenuAction new];
    quitAction->callback = [] { echo::quitApplication(); }; [targets addObject:quitAction];
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"退出 Echo" action:@selector(invoke:) keyEquivalent:@"q"];
    quit.target = quitAction; [app addItem:quit]; appEntry.submenu = app; [main insertItem:appEntry atIndex:0];
    NSApp.mainMenu = main;
    QObject::connect(owner, &QObject::destroyed, [targets] { Q_UNUSED(targets); });
}

void useDarkAppearance()
{
    [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
}

bool deferHideUntilFullscreenExit(QWindow* window, const std::function<void()>& ready)
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return false;
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    const QString phase = window->property("echoFullscreenTransition").toString();
    if (!(native.styleMask & NSWindowStyleMaskFullScreen) && phase.isEmpty()) {
        // Qt can report fullscreen without a native Space (for example in a test host).
        if (window->windowState() == Qt::WindowFullScreen) window->showNormal();
        return false;
    }
    // Keep the window visible until AppKit has removed its fullscreen Space.
    // Qt's windowStateChanged fires before that native animation has completed.
    auto* context = new QObject(window);
    const QPointer<QObject> guardedContext(context);
    const QPointer<QWindow> guardedWindow(window);
    const auto completion = ready; // Own the callback beyond this stack frame.
    id exited = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidExitFullScreenNotification object:native queue:nil
        usingBlock:^(NSNotification*) {
            if (guardedContext) QTimer::singleShot(0, guardedContext.data(), [guardedContext, completion] {
                if (!guardedContext) return;
                completion();
                if (guardedContext) guardedContext->deleteLater();
            });
        }];
    id entered = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidEnterFullScreenNotification object:native queue:nil
        usingBlock:^(NSNotification*) {
            if (guardedContext) QTimer::singleShot(0, guardedContext.data(), [guardedWindow] {
                if (guardedWindow) guardedWindow->showNormal();
            });
        }];
    QObject::connect(context, &QObject::destroyed, [exited, entered] {
        [[NSNotificationCenter defaultCenter] removeObserver:exited];
        [[NSNotificationCenter defaultCenter] removeObserver:entered];
    });
    if (phase.isEmpty()) window->showNormal();
    return true;
}

void applyQuickWindowChrome(QWindow* window)
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa") || window->property("echoQuickChrome").toBool())
        return;
    window->setProperty("echoQuickChrome", true);
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    if (!(native.styleMask & NSWindowStyleMaskFullSizeContentView))
        native.styleMask |= NSWindowStyleMaskFullSizeContentView;
    native.titlebarAppearsTransparent = YES;
    native.titleVisibility = NSWindowTitleHidden;
    native.toolbar = nil;
    native.movableByWindowBackground = NO;
    native.movable = YES;
    native.hasShadow = YES;
    if (@available(macOS 11.0, *))
        native.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;

    const QPointer<QWindow> guarded(window);
    const auto positionButtons = [guarded] {
        if (!guarded)
            return;
        NSView* content = (__bridge NSView*)reinterpret_cast<void*>(guarded->winId());
        NSWindow* host = content.window;
        if (host.styleMask & NSWindowStyleMaskFullScreen)
            return;
        // Qt can update the native style during window state transitions.
        // Reapply for older Qt versions too, which have no expanded-area hint.
        if (!(host.styleMask & NSWindowStyleMaskFullSizeContentView))
            host.styleMask |= NSWindowStyleMaskFullSizeContentView;
        host.titlebarAppearsTransparent = YES;
        host.titleVisibility = NSWindowTitleHidden;
        const qreal scale = content.bounds.size.width / qMax(1, guarded->width());
        int i = 0;
        for (NSWindowButton kind : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton }) {
            NSButton* button = [host standardWindowButton:kind];
            // The standard titlebar container is only 22 pt high and clips
            // repositioned controls. Host them in the full-size content view.
            if (button.superview != content) {
                [button removeFromSuperview];
                [content addSubview:button];
            }
            button.autoresizingMask = content.isFlipped ? NSViewMaxYMargin : NSViewMinYMargin;
            const qreal top = 24 * scale - button.frame.size.height / 2;
            const NSPoint point = NSMakePoint((18 + i++ * 20) * scale,
                content.isFlipped ? top : content.bounds.size.height - top - button.frame.size.height);
            [button setFrameOrigin:[button.superview convertPoint:point fromView:content]];
        }
    };
    positionButtons();
    QObject::connect(window, &QWindow::widthChanged, window, [window, positionButtons] {
        QTimer::singleShot(0, window, positionButtons);
    });
    QObject::connect(window, &QWindow::heightChanged, window, [window, positionButtons] {
        QTimer::singleShot(0, window, positionButtons);
    });
    QObject::connect(window, &QWindow::visibilityChanged, window, [window, positionButtons] {
        QTimer::singleShot(0, window, positionButtons);
    });
    NSMutableArray* fullscreenObservers = [NSMutableArray array];
    for (NSNotificationName name : @[NSWindowWillEnterFullScreenNotification, NSWindowDidEnterFullScreenNotification,
             NSWindowWillExitFullScreenNotification, NSWindowDidExitFullScreenNotification]) {
        id observer = [[NSNotificationCenter defaultCenter] addObserverForName:name object:native queue:nil
            usingBlock:^(NSNotification*) {
                if (!guarded) return;
                const QString phase = [name isEqual:NSWindowWillEnterFullScreenNotification] ? QStringLiteral("entering")
                    : [name isEqual:NSWindowWillExitFullScreenNotification] ? QStringLiteral("exiting") : QString();
                guarded->setProperty("echoFullscreenTransition", phase);
                if ([name isEqual:NSWindowDidEnterFullScreenNotification]) guarded->setProperty("echoNativeFullscreen", true);
                if ([name isEqual:NSWindowDidExitFullScreenNotification]) guarded->setProperty("echoNativeFullscreen", false);
                if ([name isEqual:NSWindowDidExitFullScreenNotification])
                    QTimer::singleShot(0, guarded.data(), positionButtons);
            }];
        [fullscreenObservers addObject:observer];
    }
    QObject::connect(window, &QObject::destroyed, [fullscreenObservers] {
        for (id observer in fullscreenObservers) [[NSNotificationCenter defaultCenter] removeObserver:observer];
    });
    __block BOOL dragging = NO;
    __block NSPoint pressPoint;
    __block NSPoint origin;
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
        handler:^NSEvent*(NSEvent* event) {
            QWindow* target = guarded.data();
            if (!target || !target->isVisible())
                return event;
            NSView* content = (__bridge NSView*)reinterpret_cast<void*>(target->winId());
            NSWindow* host = content.window;
            if (event.window != host)
                return event;
            if (event.type == NSEventTypeLeftMouseUp) {
                if (dragging) {
                    const NSPoint end = host.frame.origin;
                    qCDebug(windowLog) << "Quick drag" << origin.x << origin.y << "->" << end.x << end.y;
                }
                const BOOL wasDragging = dragging;
                dragging = NO;
                return wasDragging ? nil : event;
            }
            if (event.type == NSEventTypeLeftMouseDragged) {
                if (!dragging)
                    return event;
                const NSPoint point = [host convertPointToScreen:event.locationInWindow];
                [host setFrameOrigin:NSMakePoint(origin.x + point.x - pressPoint.x, origin.y + point.y - pressPoint.y)];
                return nil;
            }
            dragging = NO;
            const NSPoint point = [content convertPoint:event.locationInWindow fromView:nil];
            const qreal y = content.isFlipped ? point.y : content.bounds.size.height - point.y;
            const qreal scale = content.bounds.size.width / qMax(1, target->width());
            const bool dragArea = point.x >= 88 * scale && point.x < content.bounds.size.width - 6 * scale
                && y >= 6 * scale && (y < 24 * scale
                    || (y < 48 * scale && (point.x < 218 * scale || point.x >= 650 * scale)));
            if ((host.styleMask & NSWindowStyleMaskFullScreen) || !dragArea)
                return event;
            if (event.clickCount == 2) {
                // The first press is consumed by this native drag monitor, so
                // QML cannot recognize the second press as a double click.
                NSString* action = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
                if (action && [action caseInsensitiveCompare:@"Minimize"] == NSOrderedSame)
                    [host performMiniaturize:nil];
                else if (!action || [action caseInsensitiveCompare:@"None"] != NSOrderedSame)
                    [host performZoom:nil];
                return nil;
            }
            if (event.clickCount > 2)
                return event;
            pressPoint = [host convertPointToScreen:event.locationInWindow];
            origin = host.frame.origin;
            dragging = YES;
            [host makeKeyAndOrderFront:nil];
            return nil;
        }];
    QObject::connect(window, &QObject::destroyed, [monitor] { [NSEvent removeMonitor:monitor]; });
}

void installMediaIntegration(QWindow*, const std::function<void(MediaCommand, qint64)>& handler)
{
    g_handler = handler;
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    [center.togglePlayPauseCommand setEnabled:YES];
    [center.playCommand setEnabled:YES];
    [center.pauseCommand setEnabled:YES];
    [center.nextTrackCommand setEnabled:YES];
    [center.previousTrackCommand setEnabled:YES];
    [center.changePlaybackPositionCommand setEnabled:YES];
    [center.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        dispatchCommand(MediaCommand::Toggle, 0);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        dispatchCommand(MediaCommand::Toggle, 0);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        dispatchCommand(MediaCommand::Toggle, 0);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        dispatchCommand(MediaCommand::Next, 0);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
        dispatchCommand(MediaCommand::Previous, 0);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [center.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        auto* change = (MPChangePlaybackPositionCommandEvent*)event;
        dispatchCommand(MediaCommand::Seek, static_cast<qint64>(change.positionTime * 1000.0));
        return MPRemoteCommandHandlerStatusSuccess;
    }];
}

void updateSystemNowPlaying(const Track& track, const QImage& artwork, qint64 positionMs, qint64 durationMs, bool playing, bool force)
{
    static Track lastTrack;
    static bool lastPlaying = false;
    static bool hadArtwork = false;
    static qint64 lastPosition = 0;
    static QElapsedTimer elapsed;
    const qint64 expected = lastPosition + (lastPlaying && elapsed.isValid() ? elapsed.elapsed() : 0);
    if (!force && elapsed.isValid() && track == lastTrack && playing == lastPlaying && hadArtwork == !artwork.isNull()
        && std::abs(positionMs - expected) < 1000) return;
    lastTrack = track;
    lastPlaying = playing;
    lastPosition = positionMs;
    hadArtwork = !artwork.isNull();
    elapsed.start();
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = track.displayTitle().toNSString();
    info[MPMediaItemPropertyArtist] = track.displayArtist().toNSString();
    info[MPMediaItemPropertyAlbumTitle] = track.displayAlbum().toNSString();
    info[MPMediaItemPropertyPlaybackDuration] = @(durationMs / 1000.0);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionMs / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
    static QString cachedHash;
    static MPMediaItemArtwork* cachedArtwork = nil;
    if (cachedHash != track.artworkHash || artwork.isNull()) { cachedHash = track.artworkHash; cachedArtwork = nil; }
    if (!artwork.isNull() && !cachedArtwork) {
        QImage image = artwork.convertToFormat(QImage::Format_ARGB32);
        CGImageRef cg = image.toCGImage();
        if (cg) {
            NSImage* native = [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(image.width(), image.height())];
            CGImageRelease(cg);
            cachedArtwork = [[MPMediaItemArtwork alloc] initWithBoundsSize:native.size
                                                                                requestHandler:^NSImage*(NSSize) { return native; }];
        }
    }
    if (cachedArtwork) info[MPMediaItemPropertyArtwork] = cachedArtwork;
    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = info;
    center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

} // namespace echo
