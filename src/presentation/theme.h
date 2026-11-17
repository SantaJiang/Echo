#pragma once
#include <QColor>
#include <QString>
namespace echo {
struct UiTheme {
    QString id;
    QColor canvas;
    QColor surface;
    QColor surfaceRaised;
    QColor textPrimary;
    QColor textMuted;
    QColor accent;
    QColor onAccent;
};

[[nodiscard]] QString canonicalThemeId(const QString& id);
[[nodiscard]] const UiTheme& activeUiTheme();
void applyUiTheme(const QString& id);
void useDarkTheme();
[[nodiscard]] QString formatDuration(qint64 milliseconds);
}
