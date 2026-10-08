#include "ui/sidebar.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace echo {
namespace {

class NavDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = echo::palette();
        const QRect pill = option.rect.adjusted(2, 2, -14, -2);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(pill, 6, 6);
        }
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(pill.left() + 8, pill.center().y() - 8, 16, 16);
        icon.paint(painter, iconRect);
        painter->setPen(colors.text);
        painter->drawText(QRect(iconRect.right() + 8, pill.top(), pill.width() - 36, pill.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

void showRoundedMenu(QWidget* anchor, const QList<QPair<QString, std::function<void()>>>& actions)
{
    auto* popup = new QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    const Palette colors = echo::palette();
    popup->setStyleSheet(QStringLiteral(
        "QFrame#popupCard { background: %1; border: 1px solid rgba(255,255,255,0.14); border-radius: 10px; }"
        "QPushButton { background: transparent; border: none; border-radius: 6px; color: %2; padding: 8px 14px; text-align: left; font-size: 13px; }"
        "QPushButton:hover { background: %3; }")
                             .arg(colors.sidebar.name(), colors.text.name(), colors.selected.name()));
    auto* outer = new QVBoxLayout(popup);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* card = new QFrame(popup);
    card->setObjectName(QStringLiteral("popupCard"));
    card->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);
    for (const auto& action : actions) {
        auto* button = new QPushButton(action.first, card);
        button->setObjectName(QStringLiteral("popupItem"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(true);
        button->setMinimumWidth(148);
        QObject::connect(button, &QPushButton::clicked, popup, [popup, call = action.second] {
            popup->close();
            if (call) {
                call();
            }
        });
        layout->addWidget(button);
    }
    outer->addWidget(card);
    popup->adjustSize();
    const QPoint origin = anchor->mapToGlobal(QPoint(anchor->width() + 6, 0));
    popup->move(origin);
    popup->show();
}

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
    layout->setSpacing(6);
    auto* libraryLabel = new QLabel(QStringLiteral("资料库"), this);
    auto* playlistLabel = new QLabel(QStringLiteral("播放列表"), this);
    const Palette colors = echo::palette();
    const QString sectionStyle = QStringLiteral("color: %1; font-size: 11px; font-weight: 600; padding-left: 14px;").arg(colors.secondary.name());
    libraryLabel->setStyleSheet(sectionStyle);
    playlistLabel->setStyleSheet(sectionStyle);
    libraryLabel->setFixedHeight(22);
    playlistLabel->setFixedHeight(22);

    m_library = new QListWidget(this);
    m_library->setObjectName(QStringLiteral("libraryNav"));
    m_library->setFrameShape(QFrame::NoFrame);
    m_library->setFocusPolicy(Qt::NoFocus);
    m_library->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_library->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_library->setItemDelegate(new NavDelegate(m_library));
    m_library->setFixedHeight(36 * 4 + 6);
    const QStringList titles {QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺人"), QStringLiteral("文件夹")};
    const QList<Icon> icons {Icon::Songs, Icon::Albums, Icon::Artists, Icon::Folders};
    for (int index = 0; index < titles.size(); ++index) {
        auto* item = new QListWidgetItem(icon(icons[index], colors.text, 16), titles[index]);
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
    addButton->setToolTip(QStringLiteral("新建"));
    header->addWidget(addButton);
    connect(addButton, &QToolButton::clicked, this, [this, addButton] {
        showRoundedMenu(addButton, {
            {QStringLiteral("新建歌单"), [this] { emit createPlaylistRequested(0); }},
            {QStringLiteral("新建文件夹"), [this] { emit createFolderRequested(); }},
            {QStringLiteral("添加音乐文件夹"), [this] { emit addFolderRequested(); }},
        });
    });

    m_playlists = new QTreeWidget(this);
    m_playlists->setObjectName(QStringLiteral("playlistTree"));
    m_playlists->setHeaderHidden(true);
    m_playlists->setFocusPolicy(Qt::NoFocus);
    m_playlists->setFrameShape(QFrame::NoFrame);
    m_playlists->setIndentation(14);
    m_playlists->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlists, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole + 1).toString() == QLatin1String("playlist")) {
            m_library->clearSelection();
            emit playlistSelected(item->data(0, Qt::UserRole).toLongLong());
        }
    });
    connect(m_playlists, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = m_playlists->itemAt(position);
        if (!item) {
            return;
        }
        const qint64 id = item->data(0, Qt::UserRole).toLongLong();
        const bool folder = item->data(0, Qt::UserRole + 1).toString() == QLatin1String("folder");
        QMenu menu(this);
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

    auto* rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("sidebarRule"));
    rule->setFixedHeight(1);
    layout->addWidget(libraryLabel);
    layout->addWidget(m_library);
    layout->addSpacing(8);
    layout->addWidget(rule);
    layout->addSpacing(8);
    layout->addLayout(header);
    m_playlistEmpty = new QLabel(QStringLiteral("还没有歌单"), this);
    m_playlistEmpty->setAlignment(Qt::AlignCenter);
    m_playlistEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(colors.secondary.name()));
    layout->addWidget(m_playlists, 1);
    layout->addWidget(m_playlistEmpty, 1);
    layout->addWidget(m_status);
    setFixedWidth(232);
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
            item->setIcon(0, icon(Icon::Folders, echo::palette().secondary, 14));
            item->setSizeHint(0, QSize(0, 30));
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
        item->setIcon(0, icon(Icon::Playlist, echo::palette().text, 14));
        item->setSizeHint(0, QSize(0, 30));
        if (playlist.folderId > 0 && folderItems.contains(playlist.folderId)) {
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
