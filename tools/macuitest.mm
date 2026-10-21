#include <QWidget>
#include "platform/platform.h"
#include <QDebug>
#include <cmath>
#ifdef slots
#undef slots
#endif
#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

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

void verifyNativeNowPlaying()
{
    echo::Track track;
    track.id = 90001;
    track.title = QStringLiteral("Now Playing seek regression");
    echo::updateSystemNowPlaying(track, {}, 5000, 20000, true, true);
    echo::updateSystemNowPlaying(track, {}, 5250, 20000, true, true);
    const double elapsed = [[[MPNowPlayingInfoCenter defaultCenter] nowPlayingInfo][MPNowPlayingInfoPropertyElapsedPlaybackTime] doubleValue];
    if (std::abs(elapsed - 5.25) > .01) qFatal("Explicit sub-second seeks must update native Now Playing time");
    qInfo() << "Native Now Playing: sub-second seeks update the system clock";
}
