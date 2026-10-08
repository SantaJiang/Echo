#include "ui/tracktable.h"

#include "ui/theme.h"

#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>

namespace echo {
namespace {

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
        QRect row = option.rect;
        if (index.column() == 0) {
            row.setLeft(option.rect.left() + 8);
        }
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, colors.selected);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect, colors.hover);
        }
        const bool missing = index.data(Qt::UserRole + 1).toBool();
        const bool playing = index.data(Qt::UserRole + 2).toBool();
        QColor text = missing ? colors.secondary : colors.text;
        if (playing && index.column() == 1) {
            text = colors.accent;
        }
        painter->setPen(index.column() == 0 || index.column() == 4 ? colors.secondary : text);
        const QString value = index.data(Qt::DisplayRole).toString();
        const QRect textRect = option.rect.adjusted(index.column() == 0 ? 14 : 8, 0, -8, 0);
        painter->drawText(textRect, Qt::AlignVCenter | (index.column() == 4 ? Qt::AlignRight : Qt::AlignLeft), value);
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
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

void TrackModel::setPlayingId(qint64 id)
{
    m_playingId = id;
    if (!m_tracks.isEmpty()) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
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
        return track.id == m_playingId;
    }
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
    setModel(m_model);
    setItemDelegate(new RowDelegate(this));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setShowGrid(false);
    setWordWrap(false);
    setAlternatingRowColors(false);
    setMouseTracking(true);
    verticalHeader()->hide();
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    setColumnWidth(0, 54);
    setColumnWidth(4, 72);
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
        QVector<qint64> ids;
        const auto rows = selectionModel()->selectedRows();
        for (const QModelIndex& row : rows) {
            ids.push_back(m_model->trackAt(row.row()).id);
        }
        if (ids.isEmpty()) {
            ids.push_back(m_model->trackAt(index.row()).id);
        }
        QMenu menu(this);
        menu.addAction(QStringLiteral("播放"), this, [this, index] { emit tracksActivated(m_model->ids(), index.row()); });
        menu.addAction(QStringLiteral("下一首播放"), this, [this, ids] { emit playNextRequested(ids); });
        menu.addAction(QStringLiteral("加入待播"), this, [this, ids] { emit enqueueRequested(ids); });
        menu.addAction(QStringLiteral("加入歌单"), this, [this, ids] { emit playlistRequested(ids); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("歌曲信息"), this, [this, index] { emit infoRequested(m_model->trackAt(index.row()).id); });
        menu.addAction(QStringLiteral("在文件夹中显示"), this, [this, index] { emit revealRequested(m_model->trackAt(index.row()).id); });
        menu.addAction(QStringLiteral("移除"), this, [this, ids] { emit removeRequested(ids); });
        menu.exec(viewport()->mapToGlobal(position));
    });
}

void TrackTable::setTracks(QVector<Track> tracks)
{
    m_model->setTracks(std::move(tracks));
}

void TrackTable::setPlayingId(qint64 id)
{
    m_model->setPlayingId(id);
}

} // namespace echo
