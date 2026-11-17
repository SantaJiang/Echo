#include "platform/platform.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace echo {

void quitApplication()
{
    // Explicit quit must bypass the main window's close-to-tray handler.
    QCoreApplication::exit(0);
}

#ifndef Q_OS_MACOS
bool deferHideUntilFullscreenExit(QWindow*, const std::function<void()>&)
{
    return false;
}
#endif

void showInFileManager(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return;
    }
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

} // namespace echo
