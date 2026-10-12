#include "platform/platform.h"

#include <QImage>
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QPointer>
#include <QWidget>

#include <dispatch/dispatch.h>

#ifdef slots
#undef slots
#endif
#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

namespace echo {
namespace {

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

void useDarkAppearance()
{
    [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
}

void applyWindowChrome(QWidget* window)
{
    useDarkAppearance();
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    native.titlebarAppearsTransparent = YES;
    native.titleVisibility = NSWindowTitleHidden;
    native.styleMask |= NSWindowStyleMaskFullSizeContentView;
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
            const bool trafficLights = local.x() < 74 && local.y() < 36;
            if (!target->rect().contains(local) || local.y() >= 52 || trafficLights) {
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
