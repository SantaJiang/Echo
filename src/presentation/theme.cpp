#include "presentation/theme.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
namespace echo {
namespace {
const UiTheme kDusk { QStringLiteral("dusk"), QColor("#17151D"), QColor("#24202D"), QColor("#302C3D"), QColor("#F3EDF9"), QColor("#847E90"), QColor("#C9AAFF"), QColor("#21172C") };
const UiTheme kGlacial { QStringLiteral("glacial"), QColor("#101418"), QColor("#1C242C"), QColor("#24303A"), QColor("#E8F1F6"), QColor("#7D8E9C"), QColor("#9FD4E8"), QColor("#102028") };
const UiTheme* g_active = &kDusk;

const UiTheme& themeFor(const QString& id)
{
    if (id == QLatin1String("glacial"))
        return kGlacial;
    return kDusk;
}
}

QString canonicalThemeId(const QString& id)
{
    return themeFor(id.toLower()).id;
}

const UiTheme& activeUiTheme()
{
    return *g_active;
}

void applyUiTheme(const QString& id)
{
    g_active = &themeFor(canonicalThemeId(id));
    if (!qGuiApp)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    const UiTheme& theme = *g_active;
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

void useDarkTheme()
{
    applyUiTheme(QStringLiteral("dusk"));
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
