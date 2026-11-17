#include "presentation/theme.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
namespace echo {
namespace {
const UiTheme kDusk { QColor("#17151D"), QColor("#24202D"), QColor("#302C3D"), QColor("#F3EDF9"), QColor("#847E90"), QColor("#C9AAFF"), QColor("#21172C") };
}

const UiTheme& activeUiTheme()
{
    return kDusk;
}

void useDarkTheme()
{
    if (!qGuiApp)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    const UiTheme& theme = kDusk;
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    pal.setColor(QPalette::WindowText, theme.textPrimary);
    pal.setColor(QPalette::Base, theme.canvas);
    pal.setColor(QPalette::Text, theme.textPrimary);
    pal.setColor(QPalette::Button, theme.surfaceRaised);
    pal.setColor(QPalette::ButtonText, theme.textPrimary);
    pal.setColor(QPalette::Highlight, theme.accent);
    pal.setColor(QPalette::HighlightedText, theme.onAccent);
    pal.setColor(QPalette::PlaceholderText, theme.textMuted);
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
