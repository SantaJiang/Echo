#include "ui/tracktable.h"

#include "ui/theme.h"
#include "ui/icons.h"
#include <QScrollBar>
#include <QHelpEvent>
#include <QToolTip>

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyledItemDelegate>

#include <algorithm>
#include <functional>

namespace echo {
namespace {

class TrackHeader : public QHeaderView {
public:
    explicit TrackHeader(QWidget* parent)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
        setSectionResizeMode(QHeaderView::Fixed);
        setSectionsClickable(true);
    }

    std::function<void(int, int)> resizeBoundary;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const int boundary = boundaryAt(event->position().x());
        if (event->button() == Qt::LeftButton && boundary >= 0) {
            m_boundary = boundary;
            m_lastX = event->position().x();
            viewport()->setCursor(Qt::SplitHCursor);
            event->accept();
            return;
        }
        QHeaderView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_boundary >= 0 && (event->buttons() & Qt::LeftButton)) {
            const int x = qRound(event->position().x());
            const int delta = x - m_lastX;
            if (resizeBoundary) {
                resizeBoundary(m_boundary, delta);
            }
            m_lastX = x;
            event->accept();
            return;
        }
        m_boundary = -1;
        if (boundaryAt(event->position().x()) >= 0) {
            viewport()->setCursor(Qt::SplitHCursor);
        } else {
            viewport()->unsetCursor();
        }
        QHeaderView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_boundary >= 0 && event->button() == Qt::LeftButton) {
            m_boundary = -1;
            event->accept();
            return;
        }
        QHeaderView::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (boundaryAt(event->position().x()) >= 0) {
            event->accept();
            return;
        }
        QHeaderView::mouseDoubleClickEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        if (m_boundary < 0) {
            viewport()->unsetCursor();
        }
        QHeaderView::leaveEvent(event);
    }

private:
    int boundaryAt(qreal x) const
    {
        for (int section = 0; section + 1 < count(); ++section) {
            const int edge = sectionViewportPosition(section) + sectionSize(section);
            if (qAbs(x - edge) <= 8) {
                return section;
            }
        }
        return -1;
    }

    int m_boundary = -1;
    int m_lastX = 0;
};

QRectF rowPill(const QStyleOptionViewItem& option, const TrackTable* table)
{
    const int last = table && table->model() ? table->model()->columnCount() - 1 : 4;
    const int columnsLeft = table ? table->columnViewportPosition(0) : 0;
    const int columnsRight = table ? table->columnViewportPosition(last) + table->columnWidth(last) : option.rect.right();
    const int viewportRight = table && table->viewport() ? table->viewport()->width() : columnsRight;
    const qreal left = std::max(4, columnsLeft + 4);
    const qreal right = std::min(viewportRight - 10, columnsRight - 6);
    return QRectF(left, option.rect.top() + 2, std::max(20.0, right - left), option.rect.height() - 4);
}

class RowDelegate : public QStyledItemDelegate {
public:
    explicit RowDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = palette();
        const bool selected = option.state & QStyle::State_Selected;
        const auto* table = qobject_cast<const TrackTable*>(option.widget);
        const bool hover = table && table->hoverRow() == index.row();
        if ((selected || hover) && option.widget) {
            const QRectF row = rowPill(option, table);
            QPainterPath clip;
            clip.addRoundedRect(row, 8, 8);
            painter->setClipPath(clip, Qt::IntersectClip);
            painter->fillRect(option.rect, selected ? colors.selected : colors.hover);
            painter->setClipping(false);
        }
        const bool missing = index.data(Qt::UserRole + 1).toBool();
        const bool current = index.data(Qt::UserRole + 2).toBool();
        const bool audible = index.data(Qt::UserRole + 3).toBool();
        if (current && !selected && !hover && option.widget) {
            const QRectF row = rowPill(option, table);
            QPainterPath clip;
            clip.addRoundedRect(row, 8, 8);
            painter->setClipPath(clip, Qt::IntersectClip);
            QColor wash = colors.accent;
            wash.setAlpha(36);
            painter->fillRect(option.rect, wash);
            painter->setClipping(false);
        }
        QColor text = missing ? colors.secondary : colors.text;
        if (current && index.column() == 1) {
            text = colors.accent;
        }
        const QString value = index.data(Qt::DisplayRole).toString();
        const int rightPad = index.column() == 4 ? 20 : 8;
        QRect textRect = option.rect.adjusted(12, 0, -rightPad, 0);
        if (index.column() == 1 && table) {
            const QRect heart = table->favoriteRect(index.row());
            textRect.setLeft(heart.right() + 8);
            const bool liked = index.data(Qt::UserRole + 4).toBool();
            if (liked || hover) {
                icon(liked ? Icon::HeartFilled : Icon::Heart, liked ? colors.accent : colors.secondary, 16)
                    .paint(painter, heart.adjusted(4, 4, -4, -4));
            }
        }
        if (audible && index.column() == 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.accent);
            const qreal heights[3] = {5.0, 11.0, 7.0};
            qreal x = textRect.left();
            const qreal mid = option.rect.center().y();
            for (const qreal height : heights) {
                painter->drawRoundedRect(QRectF(x, mid - height / 2.0, 2.0, height), 1.0, 1.0);
                x += 4.0;
            }
        } else {
            QColor pen = index.column() == 0 || index.column() == 4 ? colors.secondary : text;
            if (current && index.column() == 0) {
                pen = colors.accent;
            }
            painter->setPen(pen);
            painter->drawText(textRect, Qt::AlignVCenter | (index.column() == 4 ? Qt::AlignRight : Qt::AlignLeft),
                              painter->fontMetrics().elidedText(value, Qt::ElideRight, qMax(0, textRect.width())));
        }
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(40, 40);
    }
};

} // namespace

TrackModel::TrackModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void TrackModel::setTracks(QVector<Track> tracks)
{
    if (tracks == m_tracks)
        return;
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

void TrackModel::setPlayingId(qint64 id, bool playing)
{
    m_playingId = id;
    m_audible = playing && id > 0;
    if (!m_tracks.isEmpty()) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    }
}

void TrackModel::setFavorites(const QSet<qint64>& ids)
{
    const auto previous = m_favorites;
    m_favorites = ids;
    for (int row = 0; row < m_tracks.size(); ++row) {
        if (previous.contains(m_tracks[row].id) != ids.contains(m_tracks[row].id))
            emit dataChanged(index(row, 1), index(row, 1));
    }
}

Track TrackModel::trackAt(int row) const
{
    if (row < 0 || row >= m_tracks.size()) {
        return {};
    }
    return m_tracks[row];
}

QVector<qint64> TrackModel::ids() const
{
    QVector<qint64> result;
    result.reserve(m_tracks.size());
    for (const Track& track : m_tracks) {
        result.push_back(track.id);
    }
    return result;
}

int TrackModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

int TrackModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 5;
}

QVariant TrackModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size()) {
        return {};
    }
    const Track& track = m_tracks[index.row()];
    if (role == Qt::UserRole) {
        return track.id;
    }
    if (role == Qt::UserRole + 1) {
        return track.missing;
    }
    if (role == Qt::UserRole + 2) {
        return track.id == m_playingId && m_playingId > 0;
    }
    if (role == Qt::UserRole + 3) {
        return track.id == m_playingId && m_audible;
    }
    if (role == Qt::ToolTipRole)
        return track.path + (track.missing ? QStringLiteral("\n暂时无法播放：找不到文件或来源不可用") : QString());
    if (role == Qt::UserRole + 4) return m_favorites.contains(track.id);
    if (role == Qt::AccessibleDescriptionRole && index.column() == 1)
        return m_favorites.contains(track.id) ? QStringLiteral("已喜欢，取消喜欢") : QStringLiteral("添加到我喜欢的");
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case 0:
        return track.trackNumber > 0 ? QString::number(track.trackNumber) : QString::number(index.row() + 1);
    case 1:
        return track.displayTitle();
    case 2:
        return track.displayArtist();
    case 3:
        return track.displayAlbum();
    case 4:
        return formatDuration(track.durationMs);
    default:
        return {};
    }
}

QVariant TrackModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        if (section == 4) {
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        }
        return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case 0:
        return QStringLiteral("#");
    case 1:
        return QStringLiteral("标题");
    case 2:
        return QStringLiteral("艺人");
    case 3:
        return QStringLiteral("专辑");
    case 4:
        return QStringLiteral("时间");
    default:
        return {};
    }
}

TrackTable::TrackTable(QWidget* parent)
    : QTableView(parent)
    , m_model(new TrackModel(this))
{
    auto* header = new TrackHeader(this);
    setHorizontalHeader(header);
    header->resizeBoundary = [this](int column, int delta) { resizeColumnBoundary(column, delta); };
    setModel(m_model);
    setItemDelegate(new RowDelegate(this));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setShowGrid(false);
    setWordWrap(false);
    setAlternatingRowColors(false);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    QPalette viewPalette = palette();
    viewPalette.setColor(QPalette::Highlight, echo::palette().selected);
    viewPalette.setColor(QPalette::HighlightedText, echo::palette().text);
    setPalette(viewPalette);
    verticalHeader()->hide();
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setMinimumSectionSize(40);
    horizontalHeader()->setCascadingSectionResizes(false);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    verticalHeader()->setDefaultSectionSize(40);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        emit tracksActivated(m_model->ids(), index.row());
    });
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = indexAt(position);
        if (!index.isValid()) {
            return;
        }
        if (!selectionModel()->isRowSelected(index.row(), { })) {
            selectionModel()->clearSelection();
            selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        QVector<qint64> ids;
        const auto rows = selectionModel()->selectedRows();
        for (const QModelIndex& row : rows) {
            ids.push_back(m_model->trackAt(row.row()).id);
        }
        if (ids.isEmpty()) {
            ids.push_back(m_model->trackAt(index.row()).id);
        }
        const auto clicked = m_model->trackAt(index.row());
        QMenu menu(this);
        menu.addAction(QStringLiteral("播放"), this, [this, clicked] {
            for (int row = 0; row < m_model->rowCount(); ++row) {
                const auto candidate = m_model->trackAt(row);
                if (clicked.listEntryId ? candidate.listEntryId == clicked.listEntryId : candidate.id == clicked.id) {
                    emit tracksActivated(m_model->ids(), row);
                    return;
                }
            }
        });
        menu.addAction(QStringLiteral("下一首播放"), this, [this, ids] { emit playNextRequested(ids); });
        menu.addAction(QStringLiteral("加入待播"), this, [this, ids] { emit enqueueRequested(ids); });
        menu.addAction(QStringLiteral("加入歌单"), this, [this, ids] { emit playlistRequested(ids); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("歌曲信息"), this, [this, clicked] { emit infoRequested(clicked.id); });
        menu.addAction(QStringLiteral("在文件夹中显示"), this, [this, clicked] { emit revealRequested(clicked.id); });
        if (m_playlistContext || m_favoritesContext)
            menu.addAction(m_favoritesContext ? QStringLiteral("取消喜欢") : QStringLiteral("从播放列表移除"), this,
                [this, ids] { emit removeRequested(ids); });
        if (m_model->trackAt(index.row()).missing) {
            menu.addAction(QStringLiteral("重新检查"), this, [this, clicked] { emit recheckRequested(clicked.id); });
            menu.addAction(QStringLiteral("重新定位…"), this, [this, clicked] { emit locateRequested(clicked.id); });
        }
        menu.addSeparator();
        menu.addAction(QStringLiteral("移到废纸篓…"), this, [this, ids] { emit trashRequested(ids); });
        menu.exec(viewport()->mapToGlobal(position));
    });
}

void TrackTable::setTracks(QVector<Track> tracks)
{
    m_model->setTracks(std::move(tracks));
}

void TrackTable::setTracksPreservingView(QVector<Track> tracks)
{
    auto key = [this](const Track& track) {
        return !m_favoritesContext && track.listEntryId ? -track.listEntryId : track.id;
    };
    QSet<qint64> selected;
    for (const auto& row : selectionModel()->selectedRows())
        selected.insert(key(m_model->trackAt(row.row())));
    const qint64 current = key(m_model->trackAt(currentIndex().row()));
    const int scroll = verticalScrollBar()->value();
    setTracks(std::move(tracks));
    selectionModel()->clearSelection();
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const auto id = key(m_model->trackAt(row));
        if (selected.contains(id))
            selectionModel()->select(m_model->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (id == current)
            selectionModel()->setCurrentIndex(m_model->index(row, 0), QItemSelectionModel::NoUpdate);
    }
    verticalScrollBar()->setValue(scroll);
}

QRect TrackTable::favoriteRect(int row) const
{
    const QRect cell = visualRect(m_model->index(row, 1));
    return QRect(cell.left() + 4, cell.center().y() - 12, 24, 24).intersected(cell);
}

void TrackTable::mousePressEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index.isValid() && index.column() == 1
        && favoriteRect(index.row()).contains(event->position().toPoint())) {
        m_pressedFavorite = m_model->trackAt(index.row()).id;
        event->accept();
        return;
    }
    m_pressedFavorite = 0;
    QTableView::mousePressEvent(event);
}

void TrackTable::mouseReleaseEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    const qint64 pressed = m_pressedFavorite;
    m_pressedFavorite = 0;
    if (event->button() == Qt::LeftButton && (pressed || (index.isValid() && index.column() == 1
        && favoriteRect(index.row()).contains(event->position().toPoint())))) {
        if (pressed && index.isValid() && index.column() == 1 && m_model->trackAt(index.row()).id == pressed
            && favoriteRect(index.row()).contains(event->position().toPoint())) emit favoriteRequested(pressed);
        event->accept();
        return;
    }
    QTableView::mouseReleaseEvent(event);
}

void TrackTable::mouseDoubleClickEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index.isValid() && index.column() == 1
        && favoriteRect(index.row()).contains(event->position().toPoint())) {
        m_pressedFavorite = 0;
        event->accept();
        return;
    }
    QTableView::mouseDoubleClickEvent(event);
}

bool TrackTable::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const auto index = indexAt(help->pos());
        if (index.isValid() && index.column() == 1 && favoriteRect(index.row()).contains(help->pos())) {
            QToolTip::showText(help->globalPos(), index.data(Qt::UserRole + 4).toBool()
                ? QStringLiteral("取消喜欢") : QStringLiteral("添加到我喜欢的"), viewport());
            return true;
        }
        QToolTip::hideText();
    }
    return QTableView::viewportEvent(event);
}

void TrackTable::setPlayingId(qint64 id, bool playing)
{
    m_model->setPlayingId(id, playing);
}

void TrackTable::setColumnWidths(const QVector<int>& widths)
{
    if (widths.size() != 5) {
        return;
    }
    m_preferredWidths = widths;
    for (int column = 0; column < widths.size(); ++column) {
        m_preferredWidths[column] = qMax(40, widths.at(column));
    }
    applyColumnWidths();
}

QVector<int> TrackTable::columnWidths() const
{
    QVector<int> widths;
    widths.reserve(5);
    for (int column = 0; column < 5; ++column) {
        widths.push_back(columnWidth(column));
    }
    return widths;
}

void TrackTable::mouseMoveEvent(QMouseEvent* event)
{
    const int row = indexAt(event->position().toPoint()).row();
    if (row != m_hoverRow) {
        m_hoverRow = row;
        viewport()->update();
    }
    QTableView::mouseMoveEvent(event);
}

void TrackTable::leaveEvent(QEvent* event)
{
    if (m_hoverRow != -1) {
        m_hoverRow = -1;
        viewport()->update();
    }
    QTableView::leaveEvent(event);
}

void TrackTable::resizeEvent(QResizeEvent* event)
{
    QTableView::resizeEvent(event);
    applyColumnWidths();
}

void TrackTable::applyColumnWidths()
{
    const int available = viewport()->width();
    if (available < 200) {
        return;
    }
    // Keep the outside columns compact; share the remaining space between
    // title, artist and album using the user's saved proportions.
    const int number = qBound(40, m_preferredWidths[0], available - 160);
    const int time = qBound(40, m_preferredWidths[4], available - number - 120);
    const int extra = available - number - time - 120;
    qint64 titleWeight = m_preferredWidths[1] - 40;
    qint64 artistWeight = m_preferredWidths[2] - 40;
    qint64 albumWeight = m_preferredWidths[3] - 40;
    if (titleWeight + artistWeight + albumWeight == 0) {
        titleWeight = artistWeight = albumWeight = 1;
    }
    const qint64 weight = titleWeight + artistWeight + albumWeight;
    const int title = 40 + int(extra * titleWeight / weight);
    const int artist = 40 + int(extra * artistWeight / weight);
    setColumnWidth(0, number);
    setColumnWidth(1, title);
    setColumnWidth(2, artist);
    setColumnWidth(3, available - number - time - title - artist);
    setColumnWidth(4, time);
}

void TrackTable::resizeColumnBoundary(int column, int delta)
{
    const int left = columnWidth(column);
    const int right = columnWidth(column + 1);
    delta = qBound(40 - left, delta, right - 40);
    if (delta == 0) {
        return;
    }
    setColumnWidth(column, left + delta);
    setColumnWidth(column + 1, right - delta);
    m_preferredWidths = columnWidths();
    emit columnWidthsChanged(m_preferredWidths);
}

} // namespace echo

namespace echo {
void TrackTable::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentIndex().isValid()) {
        emit tracksActivated(m_model->ids(), currentIndex().row());
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}
}
