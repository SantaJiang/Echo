#pragma once

#include "library/types.h"

#include <QImage>
#include <functional>

class QWidget;

namespace echo {

enum class MediaCommand { Toggle, Next, Previous, Seek };

void applyWindowChrome(QWidget* window);
void useDarkAppearance();
#ifdef Q_OS_MACOS
void installMenuBarIcon(QWidget* owner, const std::function<void()>& open, const std::function<void()>& settings);
#endif
void installSidebarEffect(QWidget* sidebar);
void showInFileManager(const QString& path);
void installMediaIntegration(QWidget* window, const std::function<void(MediaCommand, qint64)>& handler);
void updateSystemNowPlaying(const Track& track, const QImage& artwork, qint64 positionMs, qint64 durationMs, bool playing, bool force = false);

} // namespace echo
