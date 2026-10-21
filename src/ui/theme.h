#pragma once

#include <QColor>
#include <QString>

class QWidget;

namespace echo {

inline constexpr int ContentLeftMargin = 18;

struct Palette {
    QColor window;
    QColor sidebar;
    QColor text;
    QColor secondary;
    QColor accent;
    QColor hairline;
    QColor hover;
    QColor selected;
    QColor control;
    bool dark = false;
};

[[nodiscard]] Palette palette();
void useDarkTheme();
void polish(QWidget* root);
[[nodiscard]] QString formatDuration(qint64 milliseconds);

} // namespace echo
