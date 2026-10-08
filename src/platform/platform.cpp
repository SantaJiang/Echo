#include "platform/platform.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace echo {

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
