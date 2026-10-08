#include "ui/theme.h"

#include <QApplication>
#include <QStyleHints>
#include <QWidget>

namespace echo {

Palette palette()
{
    const bool dark = qApp && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    Palette colors;
    colors.dark = dark;
    colors.accent = QColor(QStringLiteral("#FA2D48"));
    if (dark) {
        colors.window = QColor(QStringLiteral("#1C1C1E"));
        colors.sidebar = QColor(QStringLiteral("#2C2C2E"));
        colors.text = QColor(QStringLiteral("#F5F5F7"));
        colors.secondary = QColor(QStringLiteral("#98989D"));
        colors.hairline = QColor(255, 255, 255, 24);
        colors.hover = QColor(255, 255, 255, 18);
        colors.selected = QColor(255, 255, 255, 28);
        colors.control = QColor(QStringLiteral("#F5F5F7"));
    } else {
        colors.window = QColor(QStringLiteral("#FFFFFF"));
        colors.sidebar = QColor(QStringLiteral("#F5F5F7"));
        colors.text = QColor(QStringLiteral("#1D1D1F"));
        colors.secondary = QColor(QStringLiteral("#86868B"));
        colors.hairline = QColor(0, 0, 0, 22);
        colors.hover = QColor(0, 0, 0, 12);
        colors.selected = QColor(0, 0, 0, 18);
        colors.control = QColor(QStringLiteral("#1D1D1F"));
    }
    return colors;
}

void polish(QWidget* root)
{
    const Palette colors = palette();
    root->setStyleSheet(QStringLiteral(
        "QWidget { color: %1; font-size: 13px; }"
        "QMainWindow, QStackedWidget { background: %2; }"
        "QWidget#sidebar { background: %3; }"
        "QLineEdit { background: %3; border: none; border-radius: 8px; padding: 6px 10px; }"
        "QMenu { background: %2; border: 1px solid %4; padding: 4px; }"
        "QMenu::item { padding: 6px 18px; border-radius: 6px; }"
        "QMenu::item:selected { background: %5; }"
        "QScrollBar:vertical { width: 10px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %6; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; background: %6; }"
        "QHeaderView::section { background: transparent; color: %7; border: none; padding: 4px 8px; font-size: 11px; }"
        "QTreeWidget, QListWidget, QTableView, QListView { background: transparent; border: none; outline: none; }"
        "QTableView::item { padding: 0 8px; }"
        "QSplitter::handle { background: transparent; width: 1px; }")
                             .arg(colors.text.name(), colors.window.name(), colors.sidebar.name(), colors.hairline.name(QColor::HexArgb),
                                  colors.hover.name(QColor::HexArgb), colors.accent.name(), colors.secondary.name()));
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
