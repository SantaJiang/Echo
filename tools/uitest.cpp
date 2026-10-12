#include "ui/tracktable.h"
#include "ui/theme.h"

#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QDebug>

#include <numeric>

namespace {

bool fillsViewport(const echo::TrackTable& table)
{
    const auto widths = table.columnWidths();
    return std::accumulate(widths.begin(), widths.end(), 0) == table.viewport()->width();
}

void mouse(QWidget* target, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, target->mapToGlobal(position), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

bool check(bool result, const char* message)
{
    if (!result) {
        qCritical() << message;
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
#ifdef ECHO_QT_PLUGINS
    QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
#endif
    QApplication application(argc, argv);
    echo::useDarkTheme();
    echo::TrackTable table;
    table.resize(900, 400);
    table.show();
    QApplication::processEvents();
    if (!check(fillsViewport(table), "Initial columns must fill the viewport")) {
        return 1;
    }

    int changes = 0;
    int sorts = 0;
    QObject::connect(&table, &echo::TrackTable::columnWidthsChanged, [&] { ++changes; });
    QObject::connect(table.horizontalHeader(), &QHeaderView::sectionClicked, [&] { ++sorts; });
    QWidget* header = table.horizontalHeader()->viewport();
    const auto before = table.columnWidths();
    const int edge = before[0] + before[1];
    const QPoint start(edge + 7, header->height() / 2);
    mouse(header, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    if (!check(header->cursor().shape() == Qt::SplitHCursor, "Resize cursor must appear 7 pixels from the divider")) {
        return 1;
    }
    mouse(header, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(header, QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
    mouse(header, QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
    const auto after = table.columnWidths();
    if (!check(after[1] == before[1] + 60 && after[2] == before[2] - 60 && fillsViewport(table)
                   && changes == 1 && sorts == 0,
               "Divider drag must resize neighboring columns without changing total width or sorting")) {
        return 1;
    }

    table.setColumnWidths(after);
    if (!check(table.columnWidths() == after, "Restoring columns at the same width must preserve them exactly")) {
        return 1;
    }
    table.resize(600, 400);
    QApplication::processEvents();
    if (!check(fillsViewport(table), "User-sized columns must fill a smaller window")) {
        return 1;
    }
    table.resize(1100, 400);
    QApplication::processEvents();
    if (!check(fillsViewport(table), "User-sized columns must fill a larger window")) {
        return 1;
    }
    QVector<echo::Track> tracks(100);
    for (int i = 0; i < tracks.size(); ++i) {
        tracks[i].id = i + 1;
        tracks[i].title = QStringLiteral("Song %1").arg(i);
    }
    table.setTracks(tracks);
    QApplication::processEvents();
    if (!check(fillsViewport(table) && !table.horizontalScrollBar()->isVisible(),
               "Columns must fill the viewport after a vertical scrollbar appears")) {
        return 1;
    }
    table.setTracks({});
    QApplication::processEvents();
    if (!check(fillsViewport(table), "Columns must fill the viewport after the scrollbar disappears")) {
        return 1;
    }

    const QPoint titleCenter(table.columnWidth(0) + table.columnWidth(1) / 2, header->height() / 2);
    mouse(header, QEvent::MouseButtonPress, titleCenter, Qt::LeftButton, Qt::LeftButton);
    mouse(header, QEvent::MouseButtonRelease, titleCenter, Qt::LeftButton, Qt::NoButton);
    if (!check(sorts == 1, "Clicking a column title must still sort")) {
        return 1;
    }

    // A saved layout from a wider page must also fit a hidden, narrower page
    // once it becomes visible (album, artist and queue layouts share widths).
    echo::TrackTable restored;
    restored.setColumnWidths(after);
    restored.resize(450, 300);
    restored.show();
    QApplication::processEvents();
    if (!check(fillsViewport(restored), "Restored columns must fit a differently sized page")) {
        return 1;
    }
    const auto narrow = restored.columnWidths();
    const QPoint narrowEdge(narrow[0] + narrow[1] - 7, restored.horizontalHeader()->height() / 2);
    QWidget* restoredHeader = restored.horizontalHeader()->viewport();
    mouse(restoredHeader, QEvent::MouseButtonPress, narrowEdge, Qt::LeftButton, Qt::LeftButton);
    mouse(restoredHeader, QEvent::MouseMove, narrowEdge + QPoint(1000, 0), Qt::NoButton, Qt::LeftButton);
    mouse(restoredHeader, QEvent::MouseButtonRelease, narrowEdge + QPoint(1000, 0), Qt::LeftButton, Qt::NoButton);
    if (!check(restored.columnWidth(2) == 40 && fillsViewport(restored), "Extreme drag must respect minimum column width")) {
        return 1;
    }
    qInfo() << "Track table UI regression checks passed";
    return 0;
}
