#include "platform/platform.h"

#include <QImage>
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QPointer>
#include <QLoggingCategory>
#include <QTimer>
#include <QWidget>

#include <dispatch/dispatch.h>

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
- (void)quit:(id)sender { Q_UNUSED(sender); qApp->quit(); }
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

void installMenuBarIcon(QWidget* owner, const std::function<void()>& open, const std::function<void()>& settings)
{
    NSStatusItem* item = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"music.note" accessibilityDescription:@"Echo"];
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

void useDarkAppearance()
{
    [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
}

void applyWindowChrome(QWidget* window)
{
    useDarkAppearance();
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    const bool mini = window->property("echoMiniPlayer").toBool();
    if (mini) {
        native.styleMask = NSWindowStyleMaskBorderless;
        native.level = NSFloatingWindowLevel;
        native.hidesOnDeactivate = NO;
        native.collectionBehavior = (native.collectionBehavior & ~NSWindowCollectionBehaviorMoveToActiveSpace
                                     & ~NSWindowCollectionBehaviorFullScreenPrimary)
            | NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        native.opaque = NO;
        native.backgroundColor = NSColor.clearColor;
        native.hasShadow = YES;
    }
    native.titlebarAppearsTransparent = YES;
    native.titleVisibility = NSWindowTitleHidden;
    if (!mini) native.styleMask |= NSWindowStyleMaskFullSizeContentView;
    native.toolbar = nil;
    native.movableByWindowBackground = NO;
    native.movable = YES;

    // Track the native press/drag/release sequence, including the first press
    // that activates the window, rather than handing it back to a second
    // AppKit drag loop through Qt's startSystemMove().
    const QPointer<QWidget> guardedWindow(window);
    __block BOOL dragging = NO;
    __block NSPoint pressPoint;
    __block NSPoint windowOrigin;
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
        handler:^NSEvent*(NSEvent* event) {
            QWidget* target = guardedWindow.data();
            if (!target || !target->isVisible()) {
                return event;
            }
            NSView* targetView = (__bridge NSView*)reinterpret_cast<void*>(target->winId());
            if (event.window != targetView.window) {
                return event;
            }
            if (event.type == NSEventTypeLeftMouseUp) {
                if (dragging) {
                    const NSPoint origin = targetView.window.frame.origin;
                    qCDebug(windowLog) << (target->property("echoMiniPlayer").toBool() ? "Mini drag" : "Main drag")
                        << windowOrigin.x << windowOrigin.y << "->" << origin.x << origin.y;
                }
                const BOOL wasDragging = dragging;
                dragging = NO;
                return wasDragging ? nil : event;
            }
            if (event.type == NSEventTypeLeftMouseDragged) {
                if (!dragging) {
                    return event;
                }
                const NSPoint point = [event.window convertPointToScreen:event.locationInWindow];
                [targetView.window setFrameOrigin:NSMakePoint(windowOrigin.x + point.x - pressPoint.x,
                                                             windowOrigin.y + point.y - pressPoint.y)];
                return nil;
            }
            dragging = NO;
            if (event.clickCount > 1 || (targetView.window.styleMask & NSWindowStyleMaskFullScreen)) {
                return event;
            }
            const NSPoint point = [targetView convertPoint:event.locationInWindow fromView:nil];
            const qreal y = targetView.isFlipped ? point.y : targetView.bounds.size.height - point.y;
            const QPoint local(qRound(point.x), qRound(y));
            const bool mini = target->property("echoMiniPlayer").toBool();
            const bool trafficLights = !mini && local.x() < 74 && local.y() < 36;
            if (!target->rect().contains(local) || (!mini && local.y() >= 52) || trafficLights) {
                return event;
            }
            for (QWidget* hit = QApplication::widgetAt(target->mapToGlobal(local)); hit && hit != target;
                 hit = hit->parentWidget()) {
                if (qobject_cast<QAbstractButton*>(hit) || qobject_cast<QLineEdit*>(hit)
                    || qobject_cast<QAbstractItemView*>(hit)) {
                    return event;
                }
            }
            pressPoint = [event.window convertPointToScreen:event.locationInWindow];
            windowOrigin = targetView.window.frame.origin;
            dragging = YES;
            [targetView.window makeKeyAndOrderFront:nil];
            return nil;
        }];
    QObject::connect(window, &QObject::destroyed, [monitor] {
        [NSEvent removeMonitor:monitor];
    });
}

void installSidebarEffect(QWidget*)
{
    // Qt 把控件画在 NSView 的图层上。再叠一层 NSVisualEffectView 会盖住侧边栏文字，
    // 所以这里用实色侧边栏，不插入系统材质视图。
}

void installMediaIntegration(QWidget*, const std::function<void(MediaCommand, qint64)>& handler)
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

void updateSystemNowPlaying(const Track& track, const QImage& artwork, qint64 positionMs, qint64 durationMs, bool playing)
{
    static QString lastTitle;
    static bool lastPlaying = false;
    static qint64 lastPosition = -100000;
    const qint64 delta = positionMs > lastPosition ? positionMs - lastPosition : lastPosition - positionMs;
    if (track.displayTitle() == lastTitle && playing == lastPlaying && delta < 1000) {
        return;
    }
    lastTitle = track.displayTitle();
    lastPlaying = playing;
    lastPosition = positionMs;
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = track.displayTitle().toNSString();
    info[MPMediaItemPropertyArtist] = track.displayArtist().toNSString();
    info[MPMediaItemPropertyAlbumTitle] = track.displayAlbum().toNSString();
    info[MPMediaItemPropertyPlaybackDuration] = @(durationMs / 1000.0);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionMs / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
    if (!artwork.isNull()) {
        QImage image = artwork.convertToFormat(QImage::Format_ARGB32);
        CGImageRef cg = image.toCGImage();
        if (cg) {
            NSImage* native = [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(image.width(), image.height())];
            CGImageRelease(cg);
            info[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc] initWithBoundsSize:native.size
                                                                                requestHandler:^NSImage*(NSSize) { return native; }];
        }
    }
    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = info;
    center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

} // namespace echo
