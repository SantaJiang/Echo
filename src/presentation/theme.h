#pragma once
#include <QColor>
#include <QString>
namespace echo {
struct UiTheme {
    QColor canvas;
    QColor surface;
    QColor surfaceRaised;
    QColor textPrimary;
    QColor textMuted;
    QColor accent;
    QColor onAccent;
};

[[nodiscard]] const UiTheme& activeUiTheme();
void useDarkTheme();
[[nodiscard]] QString formatDuration(qint64 milliseconds);
}
