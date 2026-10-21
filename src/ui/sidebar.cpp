#include "ui/dialogs.h"
#include "ui/sidebar.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QFrame>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace echo {
namespace {

class PlaylistTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QTreeWidget::drawRow(painter, option, index);
        if (!index.parent().isValid() && model()->hasChildren(index)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(echo::palette().secondary, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const int y = option.rect.center().y();
            const QPolygonF arrow = isExpanded(index)
                ? QPolygonF {QPointF(4, y - 2), QPointF(7, y + 1), QPointF(10, y - 2)}
                : QPolygonF {QPointF(5, y - 3), QPointF(8, y), QPointF(5, y + 3)};
            painter->drawPolyline(arrow);
            painter->restore();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && event->position().x() < 12
            && index.isValid() && !index.parent().isValid() && model()->hasChildren(index)) {
            setExpanded(index, !isExpanded(index));
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override
    {
        // The native row panel also highlights the indentation. Clear that rectangle
        // before drawing disclosure arrows; the item keeps its rounded background.
        painter->fillRect(rect, echo::palette().sidebar);
        QTreeWidget::drawBranches(painter, rect, index);
    }
};

class NavDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = echo::palette();
        const QRect pill = option.rect.adjusted(0, 1, 0, -1);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(pill, 8, 8);
        }
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(pill.left() + 18, pill.top() + (pill.height() - 16) / 2, 16, 16);
        icon.paint(painter, iconRect);
        painter->setPen(colors.text);
        QFont itemFont = option.font;
        itemFont.setWeight(QFont::Normal);
        painter->setFont(itemFont);
        const QFontMetricsF metrics(itemFont);
        const QString text = metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                               qMax(0, pill.width() - 50));
        const QRectF bounds = metrics.tightBoundingRect(text);
        const qreal baseline = pill.top() + (pill.height() - bounds.height()) / 2.0 - bounds.top();
        painter->drawText(QPointF(iconRect.x() + iconRect.width() + 8, baseline), text);
        painter->restore();
    }
};

} // namespace

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    auto* layout = new QVBoxLayout(this);
#ifdef Q_OS_MACOS
    layout->setContentsMargins(10, 42, 10, 12);
#else
    layout->setContentsMargins(10, 8, 10, 12);
#endif
    layout->setSpacing(2);
    auto* libraryLabel = new QLabel(QStringLiteral("资料库"), this);
    auto* playlistLabel = new QLabel(QStringLiteral("播放列表"), this);
    const Palette colors = echo::palette();
    const QString sectionStyle = QStringLiteral("color: %1; font-size: 11px; font-weight: 500; padding-left: 6px;").arg(colors.secondary.name());
    libraryLabel->setStyleSheet(sectionStyle);
    playlistLabel->setStyleSheet(sectionStyle);
    libraryLabel->setIndent(0);
    playlistLabel->setIndent(0);
    libraryLabel->setFixedHeight(20);
    playlistLabel->setFixedHeight(24);

    m_library = new QListWidget(this);
    m_library->setObjectName(QStringLiteral("libraryNav"));
    m_library->setFrameShape(QFrame::NoFrame);
    m_library->setFocusPolicy(Qt::NoFocus);
    m_library->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_library->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_library->setItemDelegate(new NavDelegate(m_library));
    m_library->setFixedHeight(32 * 4);
    const QStringList titles {QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺术家"), QStringLiteral("文件夹")};
    const QList<Icon> icons {Icon::Songs, Icon::Albums, Icon::Artists, Icon::Folders};
    for (int index = 0; index < titles.size(); ++index) {
        auto* item = new QListWidgetItem(icon(icons[index], colors.accent, 16), titles[index]);
        item->setSizeHint(QSize(0, 32));
        m_library->addItem(item);
    }
    m_library->setCurrentRow(0);
    connect(m_library, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_playlists->clearSelection();
            emit librarySelected(row);
        }
    });

    auto* header = new QHBoxLayout();
    header->addWidget(playlistLabel);
    header->addStretch();
    auto* addButton = new QToolButton(this);
    addButton->setObjectName(QStringLiteral("iconButton"));
    addButton->setIcon(icon(Icon::Plus, colors.secondary, 14));
    addButton->setAutoRaise(true);
    addButton->setIconSize(QSize(14, 14));
    addButton->setFixedSize(24, 24);
    addButton->setFocusPolicy(Qt::NoFocus);
    addButton->setToolTip(QStringLiteral("新建歌单"));
    header->addWidget(addButton);
    connect(addButton, &QToolButton::clicked, this, [this] { emit createPlaylistRequested(0); });

    m_playlists = new PlaylistTree(this);
    m_playlists->setObjectName(QStringLiteral("playlistTree"));
    m_playlists->setHeaderHidden(true);
    m_playlists->setFocusPolicy(Qt::NoFocus);
    m_playlists->setFrameShape(QFrame::NoFrame);
    m_playlists->setIndentation(14);
    m_playlists->setRootIsDecorated(false);
    m_playlists->setItemDelegate(new NavDelegate(m_playlists));
    m_playlists->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlists, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole + 1).toString() == QLatin1String("playlist")) {
            m_library->setCurrentRow(-1);
            emit playlistSelected(item->data(0, Qt::UserRole).toLongLong());
        }
    });
    connect(m_playlists, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = m_playlists->itemAt(position);
        if (!item) {
            return;
        }
        if (item->data(0, Qt::UserRole + 2).toBool()) return;
        const qint64 id = item->data(0, Qt::UserRole).toLongLong();
        const bool folder = item->data(0, Qt::UserRole + 1).toString() == QLatin1String("folder");
        QMenu menu(this);
        styleMenu(&menu);
        if (folder) {
            menu.addAction(QStringLiteral("在此新建歌单"), this, [this, id] { emit createPlaylistRequested(id); });
            menu.addAction(QStringLiteral("重命名"), this, [this, id] { emit renameFolderRequested(id); });
            menu.addAction(QStringLiteral("删除文件夹"), this, [this, id] { emit deleteFolderRequested(id); });
        } else {
            menu.addAction(QStringLiteral("重命名"), this, [this, id] { emit renamePlaylistRequested(id); });
            menu.addAction(QStringLiteral("删除歌单"), this, [this, id] { emit deletePlaylistRequested(id); });
        }
        menu.exec(m_playlists->viewport()->mapToGlobal(position));
    });

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(colors.secondary.name()));
    m_status->setWordWrap(true);
    m_status->hide();

    layout->addWidget(libraryLabel);
    layout->addWidget(m_library);
    layout->addSpacing(24);
    layout->addLayout(header);
    m_playlistEmpty = new QLabel(QStringLiteral("还没有歌单"), this);
    m_playlistEmpty->setAlignment(Qt::AlignCenter);
    m_playlistEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(colors.secondary.name()));
    layout->addWidget(m_playlists, 1);
    layout->addWidget(m_playlistEmpty, 1);
    layout->addWidget(m_status);
    auto* settings = new QListWidget(this);
    settings->setObjectName(QStringLiteral("settingsNav"));
    settings->setAccessibleName(QStringLiteral("设置"));
    settings->setFrameShape(QFrame::NoFrame);
    settings->setFocusPolicy(Qt::NoFocus);
    settings->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settings->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settings->setItemDelegate(new NavDelegate(settings));
    settings->setMouseTracking(true);
    settings->setFixedHeight(32);
    settings->setCursor(Qt::PointingHandCursor);
    auto* settingsItem = new QListWidgetItem(icon(Icon::Settings, colors.accent, 16), QStringLiteral("设置"));
    settingsItem->setSizeHint(QSize(0, 32));
    settings->addItem(settingsItem);
    connect(settings, &QListWidget::itemClicked, this, [this, settings] {
        emit settingsRequested();
        settings->clearSelection();
    });
    layout->addWidget(settings);
    setFixedWidth(200);
}

void Sidebar::setPlaylists(const QVector<PlaylistFolder>& folders, const QVector<Playlist>& playlists)
{
    m_rebuilding = true;
    m_playlists->clear();
    QHash<qint64, QTreeWidgetItem*> folderItems;
    QVector<PlaylistFolder> pending = folders;
    int guard = 0;
    while (!pending.isEmpty() && guard < 8) {
        QVector<PlaylistFolder> later;
        for (const PlaylistFolder& folder : pending) {
            auto* item = new QTreeWidgetItem(QStringList {folder.name});
            item->setData(0, Qt::UserRole, folder.id);
            item->setData(0, Qt::UserRole + 1, QStringLiteral("folder"));
            item->setIcon(0, icon(Icon::Folders, echo::palette().accent, 14));
            item->setSizeHint(0, QSize(0, 32));
            if (folder.parentId <= 0) {
                m_playlists->addTopLevelItem(item);
                folderItems.insert(folder.id, item);
            } else if (folderItems.contains(folder.parentId)) {
                folderItems.value(folder.parentId)->addChild(item);
                folderItems.insert(folder.id, item);
            } else {
                later.push_back(folder);
            }
        }
        pending = later;
        ++guard;
    }
    for (const Playlist& playlist : playlists) {
        auto* item = new QTreeWidgetItem(QStringList {playlist.name});
        item->setData(0, Qt::UserRole, playlist.id);
        item->setData(0, Qt::UserRole + 1, QStringLiteral("playlist"));
        item->setData(0, Qt::UserRole + 2, playlist.isFavorites());
        item->setIcon(0, icon(playlist.isFavorites() ? Icon::HeartFilled : Icon::Playlist,
                             echo::palette().accent, 14));
        item->setSizeHint(0, QSize(0, 32));
        if (playlist.isFavorites()) {
            m_playlists->insertTopLevelItem(0, item);
        } else if (playlist.folderId > 0 && folderItems.contains(playlist.folderId)) {
            folderItems.value(playlist.folderId)->addChild(item);
        } else {
            m_playlists->addTopLevelItem(item);
        }
    }
    m_playlists->expandAll();
    const bool empty = m_playlists->topLevelItemCount() == 0;
    m_playlistEmpty->setVisible(empty);
    m_playlists->setVisible(!empty);
    m_rebuilding = false;
}

void Sidebar::setScanText(const QString& text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void Sidebar::setSelectedLibrary(int index)
{
    m_library->setCurrentRow(index);
}

} // namespace echo
