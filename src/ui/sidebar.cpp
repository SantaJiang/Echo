#include "ui/sidebar.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace echo {

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(8);
    auto* libraryLabel = new QLabel(QStringLiteral("资料库"), this);
    auto* playlistLabel = new QLabel(QStringLiteral("播放列表"), this);
    const Palette colors = echo::palette();
    libraryLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; font-weight: 600;").arg(colors.secondary.name()));
    playlistLabel->setStyleSheet(libraryLabel->styleSheet());

    m_library = new QListWidget(this);
    m_library->setFrameShape(QFrame::NoFrame);
    m_library->setSpacing(2);
    const QStringList titles {QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺人"), QStringLiteral("文件夹")};
    const QList<Icon> icons {Icon::Songs, Icon::Albums, Icon::Artists, Icon::Folders};
    for (int index = 0; index < titles.size(); ++index) {
        auto* item = new QListWidgetItem(icon(icons[index], colors.text, 16), titles[index]);
        item->setSizeHint(QSize(0, 34));
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
    auto* addButton = new QPushButton(this);
    addButton->setIcon(icon(Icon::Plus, colors.secondary, 14));
    addButton->setFlat(true);
    addButton->setFixedSize(24, 24);
    header->addWidget(addButton);
    connect(addButton, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("新建歌单"), this, [this] { emit createPlaylistRequested(0); });
        menu.addAction(QStringLiteral("新建文件夹"), this, [this] { emit createFolderRequested(); });
        menu.addAction(QStringLiteral("添加音乐文件夹"), this, [this] { emit addFolderRequested(); });
        menu.exec(QCursor::pos());
    });

    m_playlists = new QTreeWidget(this);
    m_playlists->setHeaderHidden(true);
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

    layout->addWidget(libraryLabel);
    layout->addWidget(m_library);
    layout->addLayout(header);
    layout->addWidget(m_playlists, 1);
    layout->addWidget(m_status);
    setMinimumWidth(220);
    setMaximumWidth(280);
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
    m_rebuilding = false;
}

void Sidebar::setScanText(const QString& text)
{
    m_status->setText(text);
}

void Sidebar::setSelectedLibrary(int index)
{
    m_library->setCurrentRow(index);
}

} // namespace echo
