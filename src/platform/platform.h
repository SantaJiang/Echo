#pragma once

#include "library/types.h"

#include <QImage>
#include <QVariantList>
#include <QPoint>
#include <functional>

class QObject;
class QWindow;

namespace echo {

enum class MediaCommand { Toggle, Next, Previous, Seek };

void quitApplication();
void applyMiniWindowChrome(QWindow* window);
void applyQuickWindowChrome(QWindow* window);
// Returns true when ready will run after the native fullscreen transition finishes.
bool deferHideUntilFullscreenExit(QWindow* window, const std::function<void()>& ready);
void useDarkAppearance();
void installTrayIcon(QObject* owner, const std::function<void()>& open, const std::function<void()>& settings,
    const std::function<void(const QPoint&)>& context);
void installApplicationMenu(QObject* owner, const QVariantList& menus, const std::function<void(int)>& invoke);
void showInFileManager(const QString& path);
void installMediaIntegration(QWindow* window, const std::function<void(MediaCommand, qint64)>& handler);
void updateSystemNowPlaying(const Track& track, const QImage& artwork, qint64 positionMs, qint64 durationMs, bool playing, bool force = false);

} // namespace echo
