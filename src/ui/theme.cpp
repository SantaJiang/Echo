#include "ui/theme.h"

#include <QApplication>
#include <QStyleHints>
#include <QWidget>

namespace echo {

Palette palette()
{
    Palette colors;
    colors.dark = true;
    colors.accent = QColor(QStringLiteral("#FA2D48"));
    colors.window = QColor(QStringLiteral("#1C1C1E"));
    colors.sidebar = QColor(QStringLiteral("#2C2C2E"));
    colors.text = QColor(QStringLiteral("#F5F5F7"));
    colors.secondary = QColor(QStringLiteral("#98989D"));
    colors.hairline = QColor(255, 255, 255, 36);
    colors.hover = QColor(QStringLiteral("#3A3A3C"));
    colors.selected = QColor(QStringLiteral("#48484A"));
    colors.control = QColor(QStringLiteral("#F5F5F7"));
    return colors;
}

void useDarkTheme()
{
    if (!qApp) {
        return;
    }
    qApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QPalette pal;
    const Palette colors = palette();
    pal.setColor(QPalette::Window, colors.window);
    pal.setColor(QPalette::WindowText, colors.text);
    pal.setColor(QPalette::Base, colors.window);
    pal.setColor(QPalette::AlternateBase, colors.sidebar);
    pal.setColor(QPalette::Text, colors.text);
    pal.setColor(QPalette::Button, colors.sidebar);
    pal.setColor(QPalette::ButtonText, colors.text);
    pal.setColor(QPalette::Highlight, colors.selected);
    pal.setColor(QPalette::HighlightedText, colors.text);
    pal.setColor(QPalette::PlaceholderText, colors.secondary);
    pal.setColor(QPalette::ToolTipBase, colors.sidebar);
    pal.setColor(QPalette::ToolTipText, colors.text);
    qApp->setPalette(pal);
}

void polish(QWidget* root)
{
    const Palette colors = palette();
    const QString line = colors.dark ? QStringLiteral("rgba(255,255,255,0.22)") : QStringLiteral("#8E8E93");
    const QString player = colors.dark ? QStringLiteral("#2C2C2E") : QStringLiteral("#ECECF0");
    root->setStyleSheet(QStringLiteral(
        "QWidget { color: %1; font-size: 13px; }"
        "QMainWindow, QStackedWidget { background: %2; }"
        "QWidget#sidebar { background: %3; }"
        "QWidget#captionBar { background: %3; border-bottom: 1px solid %4; }"
        "QFrame#contentDivider { background: %4; }"
        "QWidget#playerBar { background: %9; border-top: 1px solid %4; }"
        "QWidget#queuePanel { background: %3; border-left: 1px solid %4; }"
        "QFrame#sidebarRule { background: %4; border: none; max-height: 1px; }"
        "QLineEdit#search { background: %3; border: none; border-radius: 8px; padding: 0 8px; }"
        "QLineEdit { background: %2; border: 1px solid %4; border-radius: 8px; padding: 6px 10px; }"
        "QScrollArea#searchScroll { background: transparent; border: none; }"
        "QScrollArea#searchScroll > QWidget > QWidget { background: transparent; }"
        "QListWidget#albumGrid, QListWidget#artistList, QTreeWidget#folderTree { background: transparent; border: none; }"
        "QListWidget#artistList::item { padding: 6px 10px; border-radius: 6px; }"
        "QListWidget#artistList::item:selected { background: %8; }"
        "QListWidget#libraryNav { background: transparent; padding: 0 10px; }"
        "QListWidget#libraryNav::item { background: transparent; border: none; }"
        "QListWidget#libraryNav::item:selected { background: transparent; }"
        "QListWidget#libraryNav::item:hover { background: transparent; }"
        "QTreeWidget#playlistTree::item { height: 28px; border-radius: 8px; }"
        "QTreeWidget#playlistTree::item:selected { background: %5; }"
        "QTreeWidget#folderTree { selection-background-color: %8; selection-color: %1; }"
        "QTreeWidget#folderTree::item:selected { background: %8; color: %1; }"
        "QToolButton { background: transparent; border: none; }"
        "QToolButton:hover { background: %5; border-radius: 8px; }"
        "QPushButton { background: transparent; border: 1px solid %4; border-radius: 16px; padding: 6px 14px; }"
        "QPushButton:hover { background: %5; }"
        "QPushButton#primaryButton, QPushButton#playFolder { background: %1; color: %2; border: none; border-radius: 16px; padding: 6px 18px; }"
        "QPushButton#primaryButton:hover, QPushButton#playFolder:hover { background: %1; }"
        "QPushButton#secondaryButton, QPushButton#backButton { background: transparent; color: %1; border: 1px solid %4; border-radius: 16px; padding: 6px 16px; }"
        "QPushButton#backButton { border: none; padding: 4px 2px; font-weight: 600; }"
        "QPushButton#iconButton, QToolButton#iconButton { border: none; padding: 0; background: transparent; }"
        "QMenu { background: %3; border: 1px solid %4; border-radius: 10px; padding: 6px; }"
        "QMenu::item { padding: 6px 16px; border-radius: 6px; }"
        "QMenu::item:selected { background: %8; }"
        "QFrame#popupCard { background: %3; border: 1px solid %4; border-radius: 10px; }"
        "QPushButton#popupItem { background: transparent; border: none; border-radius: 6px; padding: 6px 16px; text-align: left; color: %1; }"
        "QPushButton#popupItem:hover { background: %8; }"
        "QScrollBar:vertical { width: 10px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 24px; }"
        "QScrollBar:horizontal { height: 10px; background: transparent; }"
        "QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 24px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
        "QSlider#seek::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider#seek::sub-page:horizontal { background: %6; border-radius: 2px; }"
        "QSlider#seek::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; background: %6; }"
        "QSlider#volume::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider#volume::sub-page:horizontal { background: %1; border-radius: 2px; }"
        "QSlider#volume::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; background: %1; }"
        "QHeaderView::section { background: transparent; color: %7; border: none; border-bottom: 1px solid %4; padding: 0 8px; font-size: 11px; font-weight: 600; }"
        "QTreeWidget, QListWidget, QTableView, QListView { background: transparent; border: none; outline: none; }"
        "QTableView { selection-background-color: transparent; selection-color: %1; }"
        "QTableView::item { padding: 0 8px; background: transparent; }"
        "QTableView::item:selected { background: transparent; color: %1; }"
        "QSplitter::handle { background: transparent; width: 1px; }")
                             .arg(colors.text.name(), colors.window.name(), colors.sidebar.name(), line, colors.hover.name(),
                                  colors.accent.name(), colors.secondary.name(), colors.selected.name(), player));
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
