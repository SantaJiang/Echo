#include "platform/platform.h"

#include <QImage>
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

void applyWindowChrome(QWidget* window)
{
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* native = view.window;
    native.titlebarAppearsTransparent = YES;
    native.titleVisibility = NSWindowTitleHidden;
    native.styleMask |= NSWindowStyleMaskFullSizeContentView;
    native.toolbarStyle = NSWindowToolbarStyleUnified;
    if (native.toolbar == nil) {
        native.toolbar = [[NSToolbar alloc] initWithIdentifier:@"echo.toolbar"];
    }
}

void installSidebarEffect(QWidget* sidebar)
{
    sidebar->setAttribute(Qt::WA_TranslucentBackground);
    sidebar->setStyleSheet(QStringLiteral("QWidget#sidebar { background: transparent; }"));
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(sidebar->winId());
    NSVisualEffectView* effect = [[NSVisualEffectView alloc] initWithFrame:view.bounds];
    effect.material = NSVisualEffectMaterialSidebar;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateFollowsWindowActiveState;
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [view addSubview:effect positioned:NSWindowBelow relativeTo:nil];
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
