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
// Whether the platform can paint a system backdrop behind transparent QML.
// Safe to call before a window exists.
bool supportsWindowBackdrop();
// Installs the native window chrome. Returns whether a system backdrop is
// actually showing through: Qt paints opaque by default, so a false result
// means the window must keep its opaque clear colour.
bool applyQuickWindowChrome(QWindow* window);
// Toggles the backdrop of an already chromed window. Returns whether it is
// active afterwards.
bool setWindowBackdrop(QWindow* window, bool enabled);
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
