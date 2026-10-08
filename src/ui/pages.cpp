#include "ui/pages.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace echo {
namespace {

QLabel* titleLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700;"));
    return label;
}

void fillAlbumItem(QListWidget* list, const Album& album, ArtworkCache* cache)
{
    auto* item = new QListWidgetItem(album.title + QStringLiteral("\n") + album.artist);
    item->setData(Qt::UserRole, album.key);
    item->setSizeHint(QSize(168, 210));
    if (cache && !album.artworkHash.isEmpty()) {
        item->setIcon(QIcon(cache->pixmap(album.artworkHash, 320)));
    } else {
        item->setIcon(icon(Icon::Albums, echo::palette().secondary, 48));
    }
    list->addItem(item);
}

} // namespace

SongsPage::SongsPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_table(new TrackTable(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 18, 18, 8);
    layout->addWidget(titleLabel(QStringLiteral("歌曲"), this));
    layout->addWidget(m_table, 1);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        const QString column = section == 2 ? QStringLiteral("artist") : section == 3 ? QStringLiteral("album") : QStringLiteral("title");
        if (m_sort == column) {
            m_desc = !m_desc;
        } else {
            m_sort = column;
            m_desc = false;
        }
        reload();
    });
}

void SongsPage::reload()
{
    m_table->setTracks(m_library->allTracks(m_sort, m_desc));
}

void SongsPage::setPlayingId(qint64 id)
{
    m_table->setPlayingId(id);
}

AlbumsPage::AlbumsPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    m_stack = new QStackedWidget(this);
    auto* gridPage = new QWidget(m_stack);
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(28, 18, 18, 8);
    gridLayout->addWidget(titleLabel(QStringLiteral("专辑"), gridPage));
    m_grid = new QListWidget(gridPage);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setSpacing(16);
    m_grid->setIconSize(QSize(160, 160));
    m_grid->setWordWrap(true);
    gridLayout->addWidget(m_grid, 1);

    m_detailPage = new QWidget(m_stack);
    auto* detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(28, 18, 18, 8);
    auto* back = new QPushButton(QStringLiteral("专辑"), m_detailPage);
    back->setFlat(true);
    auto* header = new QHBoxLayout();
    auto* art = new QLabel(m_detailPage);
    art->setObjectName(QStringLiteral("albumArt"));
    art->setFixedSize(180, 180);
    auto* meta = new QVBoxLayout();
    auto* name = new QLabel(m_detailPage);
    name->setObjectName(QStringLiteral("albumName"));
    name->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: 700;"));
    auto* artist = new QLabel(m_detailPage);
    artist->setObjectName(QStringLiteral("albumArtist"));
    artist->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().accent.name()));
    auto* play = new QPushButton(QStringLiteral("播放"), m_detailPage);
    auto* shuffle = new QPushButton(QStringLiteral("随机播放"), m_detailPage);
    meta->addWidget(name);
    meta->addWidget(artist);
    meta->addSpacing(12);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(play);
    buttons->addWidget(shuffle);
    buttons->addStretch();
    meta->addLayout(buttons);
    meta->addStretch();
    header->addWidget(art);
    header->addLayout(meta);
    m_detail = new TrackTable(m_detailPage);
    detailLayout->addWidget(back, 0, Qt::AlignLeft);
    detailLayout->addLayout(header);
    detailLayout->addWidget(m_detail, 1);

    m_stack->addWidget(gridPage);
    m_stack->addWidget(m_detailPage);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { openAlbum(item->data(Qt::UserRole).toString()); });
    connect(back, &QPushButton::clicked, this, [this] { m_stack->setCurrentIndex(0); });
    connect(play, &QPushButton::clicked, this, [this] {
        emit albumPlayRequested(m_detail->property("albumKey").toString(), false);
    });
    connect(shuffle, &QPushButton::clicked, this, [this] {
        emit albumPlayRequested(m_detail->property("albumKey").toString(), true);
    });
    connect(artist, &QLabel::linkActivated, this, [this](const QString& key) { emit artistRequested(key); });
}

void AlbumsPage::reload()
{
    m_grid->clear();
    for (const Album& album : m_library->albums(m_artistFilter)) {
        fillAlbumItem(m_grid, album, m_library->artwork());
    }
}

void AlbumsPage::showArtist(const QString& artistKey)
{
    m_artistFilter = artistKey;
    reload();
}

void AlbumsPage::setPlayingId(qint64 id)
{
    m_detail->setPlayingId(id);
}

void AlbumsPage::openAlbum(const QString& key)
{
    showAlbum(key);
    m_stack->setCurrentIndex(1);
}

void AlbumsPage::showAlbum(const QString& key)
{
    const auto tracks = m_library->albumTracks(key);
    m_detail->setTracks(tracks);
    m_detail->setProperty("albumKey", key);
    auto* name = m_detailPage->findChild<QLabel*>(QStringLiteral("albumName"));
    auto* artist = m_detailPage->findChild<QLabel*>(QStringLiteral("albumArtist"));
    auto* art = m_detailPage->findChild<QLabel*>(QStringLiteral("albumArt"));
    if (!tracks.isEmpty()) {
        name->setText(tracks.first().displayAlbum());
        artist->setTextFormat(Qt::RichText);
        artist->setTextInteractionFlags(Qt::TextBrowserInteraction);
        artist->setOpenExternalLinks(false);
        artist->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
                            .arg(tracks.first().artistKey, echo::palette().accent.name(), tracks.first().displayAlbumArtist().toHtmlEscaped()));
        const QPixmap pixmap = m_library->artwork()->pixmap(tracks.first().artworkHash, 360);
        art->setPixmap(pixmap.isNull() ? QPixmap() : pixmap.scaled(180, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

ArtistsPage::ArtistsPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 18, 12, 8);
    m_artists = new QListWidget(this);
    m_artists->setFixedWidth(240);
    auto* right = new QVBoxLayout();
    m_albums = new QListWidget(this);
    m_albums->setViewMode(QListView::IconMode);
    m_albums->setIconSize(QSize(120, 120));
    m_albums->setFixedHeight(180);
    m_albums->setMovement(QListView::Static);
    m_table = new TrackTable(this);
    right->addWidget(titleLabel(QStringLiteral("艺人"), this));
    right->addWidget(m_albums);
    right->addWidget(m_table, 1);
    layout->addWidget(m_artists);
    layout->addLayout(right, 1);
    connect(m_artists, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) {
            return;
        }
        const QString key = m_artists->item(row)->data(Qt::UserRole).toString();
        m_albums->clear();
        for (const Album& album : m_library->albums(key)) {
            fillAlbumItem(m_albums, album, m_library->artwork());
        }
        m_table->setTracks(m_library->artistTracks(key));
    });
    connect(m_albums, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit albumPlayRequested(item->data(Qt::UserRole).toString(), false);
    });
}

void ArtistsPage::reload()
{
    m_artists->clear();
    for (const Artist& artist : m_library->artists()) {
        auto* item = new QListWidgetItem(QStringLiteral("%1    %2 张专辑").arg(artist.name).arg(artist.albums));
        item->setData(Qt::UserRole, artist.key);
        item->setSizeHint(QSize(0, 36));
        m_artists->addItem(item);
    }
    if (m_artists->count() > 0) {
        m_artists->setCurrentRow(0);
    }
}

void ArtistsPage::selectArtist(const QString& key)
{
    for (int row = 0; row < m_artists->count(); ++row) {
        if (m_artists->item(row)->data(Qt::UserRole).toString() == key) {
            m_artists->setCurrentRow(row);
            return;
        }
    }
}

void ArtistsPage::setPlayingId(qint64 id)
{
    m_table->setPlayingId(id);
}

FoldersPage::FoldersPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 18, 12, 8);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(260);
    auto* right = new QVBoxLayout();
    auto* play = new QPushButton(QStringLiteral("播放此文件夹"), this);
    m_table = new TrackTable(this);
    right->addWidget(titleLabel(QStringLiteral("文件夹"), this));
    right->addWidget(play, 0, Qt::AlignLeft);
    right->addWidget(m_table, 1);
    layout->addWidget(m_tree);
    layout->addLayout(right, 1);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        m_table->setTracks(m_library->folderTracks(item->data(0, Qt::UserRole).toString(), false));
    });
    connect(play, &QPushButton::clicked, this, [this] {
        if (auto* item = m_tree->currentItem()) {
            emit playFolderRequested(item->data(0, Qt::UserRole).toString());
        }
    });
}

void FoldersPage::reload()
{
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> nodes;
    const auto folders = m_library->folders();
    for (const QString& folder : folders) {
        const QStringList parts = folder.split(u'/', Qt::SkipEmptyParts);
        QString built;
        QTreeWidgetItem* parent = nullptr;
        for (const QString& part : parts) {
            built += QLatin1Char('/') + part;
            if (nodes.contains(built)) {
                parent = nodes.value(built);
                continue;
            }
            auto* item = new QTreeWidgetItem(QStringList {part});
            item->setData(0, Qt::UserRole, built);
            item->setIcon(0, icon(Icon::Folders, echo::palette().secondary, 14));
            if (parent) {
                parent->addChild(item);
            } else {
                m_tree->addTopLevelItem(item);
            }
            nodes.insert(built, item);
            parent = item;
        }
    }
    m_tree->expandToDepth(1);
}

void FoldersPage::setPlayingId(qint64 id)
{
    m_table->setPlayingId(id);
}

PlaylistPage::PlaylistPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_table(new TrackTable(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 18, 18, 8);
    auto* title = new QLabel(QStringLiteral("歌单"), this);
    title->setObjectName(QStringLiteral("playlistTitle"));
    title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700;"));
    layout->addWidget(title);
    layout->addWidget(m_table, 1);
}

void PlaylistPage::setPlaylist(qint64 id)
{
    m_id = id;
    reload();
}

void PlaylistPage::reload()
{
    if (m_id <= 0) {
        return;
    }
    QString name;
    for (const Playlist& playlist : m_library->playlists()) {
        if (playlist.id == m_id) {
            name = playlist.name;
        }
    }
    if (auto* title = findChild<QLabel*>(QStringLiteral("playlistTitle"))) {
        title->setText(name);
    }
    QVector<Track> tracks;
    for (const qint64 id : m_library->playlistTrackIds(m_id)) {
        tracks.push_back(m_library->track(id));
    }
    m_table->setTracks(tracks);
}

void PlaylistPage::setPlayingId(qint64 id)
{
    m_table->setPlayingId(id);
}

SearchPage::SearchPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 18, 18, 8);
    layout->addWidget(titleLabel(QStringLiteral("搜索"), this));
    auto* columns = new QHBoxLayout();
    m_albums = new QListWidget(this);
    m_artists = new QListWidget(this);
    m_playlists = new QListWidget(this);
    columns->addWidget(m_artists);
    columns->addWidget(m_albums);
    columns->addWidget(m_playlists);
    m_tracks = new TrackTable(this);
    layout->addLayout(columns);
    layout->addWidget(m_tracks, 1);
    connect(m_albums, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit albumSelected(item->data(Qt::UserRole).toString());
    });
    connect(m_artists, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit artistSelected(item->data(Qt::UserRole).toString());
    });
    connect(m_playlists, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit playlistSelected(item->data(Qt::UserRole).toLongLong());
    });
}

void SearchPage::search(const QString& text)
{
    m_tracks->setTracks(m_library->searchTracks(text));
    m_albums->clear();
    for (const Album& album : m_library->searchAlbums(text)) {
        auto* item = new QListWidgetItem(album.title + QStringLiteral(" — ") + album.artist);
        item->setData(Qt::UserRole, album.key);
        m_albums->addItem(item);
    }
    m_artists->clear();
    for (const Artist& artist : m_library->searchArtists(text)) {
        auto* item = new QListWidgetItem(artist.name);
        item->setData(Qt::UserRole, artist.key);
        m_artists->addItem(item);
    }
    m_playlists->clear();
    for (const Playlist& playlist : m_library->searchPlaylists(text)) {
        auto* item = new QListWidgetItem(playlist.name);
        item->setData(Qt::UserRole, playlist.id);
        m_playlists->addItem(item);
    }
}

void SearchPage::setPlayingId(qint64 id)
{
    m_tracks->setPlayingId(id);
}

} // namespace echo
