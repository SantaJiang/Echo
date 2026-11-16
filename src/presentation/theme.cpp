#include "presentation/theme.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
namespace echo {
void useDarkTheme()
{
    if (!qGuiApp) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#24202d"));
    pal.setColor(QPalette::WindowText, QColor("#eee8f5"));
    pal.setColor(QPalette::Base, QColor("#17151d"));
    pal.setColor(QPalette::Text, QColor("#eee8f5"));
    pal.setColor(QPalette::Button, QColor("#302938"));
    pal.setColor(QPalette::ButtonText, QColor("#eee8f5"));
    pal.setColor(QPalette::Highlight, QColor("#806398"));
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::PlaceholderText, QColor("#8e849c"));
    QGuiApplication::setPalette(pal);
}
QString formatDuration(qint64 milliseconds)
{
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const qint64 total = milliseconds / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar(u'0'))
            .arg(seconds, 2, 10, QChar(u'0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar(u'0'));
}

} // namespace echo
