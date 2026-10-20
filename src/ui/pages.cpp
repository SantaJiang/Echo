#include "ui/pages.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace echo {
namespace {

class AlbumCardDelegate : public QStyledItemDelegate {
public:
    explicit AlbumCardDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = echo::palette();
        const int coverSide = std::min(160, std::max(72, option.rect.width() - 16));
        const QRect cover(option.rect.left() + 8, option.rect.top() + 6, coverSide, coverSide);
        if (option.state & QStyle::State_Selected || option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(QRectF(cover.adjusted(-4, -4, 4, 28)), 10, 10);
        }
        const QIcon artwork = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QPixmap pixmap = artwork.pixmap(320, 320);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(cover), 8, 8);
        painter->setClipPath(clip);
        if (!pixmap.isNull()) {
            painter->drawPixmap(cover, pixmap);
        } else {
            painter->fillRect(cover, colors.sidebar);
        }
        painter->setClipping(false);
        const QStringList lines = index.data(Qt::DisplayRole).toString().split(u'\n');
        const QRect title(cover.left(), cover.bottom() + 8, cover.width(), 18);
        const QRect artist(cover.left(), title.bottom(), cover.width(), 16);
        painter->setPen(colors.text);
        painter->drawText(title, Qt::AlignHCenter | Qt::AlignVCenter, painter->fontMetrics().elidedText(lines.value(0), Qt::ElideRight, title.width()));
        painter->setPen(colors.secondary);
        painter->drawText(artist, Qt::AlignHCenter | Qt::AlignVCenter, painter->fontMetrics().elidedText(lines.value(1), Qt::ElideRight, artist.width()));
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(176, 214);
    }
};

void fillAlbumItem(QListWidget* list, const Album& album, ArtworkCache* cache)
{
    auto* item = new QListWidgetItem(album.title + QStringLiteral("\n") + album.artist);
    item->setData(Qt::UserRole, album.key);
    item->setSizeHint(QSize(176, 214));
    if (cache && !album.artworkHash.isEmpty()) {
        item->setIcon(QIcon(cache->pixmap(album.artworkHash, 320)));
    } else {
        item->setIcon(QIcon(placeholderCover(320)));
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
    layout->setContentsMargins(32, 4, 24, 8);
    m_empty = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->addStretch(2);
    auto* headline = new QLabel(QStringLiteral("资料库还是空的"), m_empty);
    headline->setAlignment(Qt::AlignCenter);
    headline->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 650;"));
    auto* hint = new QLabel(QStringLiteral("添加一个音乐文件夹后，歌曲、专辑和艺人会出现在这里。"), m_empty);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(echo::palette().secondary.name()));
    auto* add = new QPushButton(QStringLiteral("添加音乐文件夹"), m_empty);
    add->setFixedWidth(160);
    add->setFixedHeight(32);
    emptyLayout->addWidget(headline);
    emptyLayout->addWidget(hint);
    emptyLayout->addSpacing(8);
    emptyLayout->addWidget(add, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(3);
    add->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: none; border-radius: 16px; padding: 6px 16px; }")
                           .arg(echo::palette().text.name(), echo::palette().window.name()));
    layout->addWidget(m_empty, 1);
    layout->addWidget(m_table, 1);
    connect(add, &QPushButton::clicked, this, &SongsPage::addFolderRequested);
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
    const auto tracks = m_library->allTracks(m_sort, m_desc);
    m_table->setTracks(tracks);
    const bool empty = tracks.isEmpty();
    m_empty->setVisible(empty);
    m_table->setVisible(!empty);
}

void SongsPage::setPlayingId(qint64 id, bool playing)
{
    m_table->setPlayingId(id, playing);
}

AlbumsPage::AlbumsPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    m_stack = new QStackedWidget(this);
    auto* gridPage = new QWidget(m_stack);
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(32, 4, 24, 8);
    m_grid = new QListWidget(gridPage);
    m_grid->setObjectName(QStringLiteral("albumGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setFlow(QListView::LeftToRight);
    m_grid->setWrapping(true);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setSpacing(8);
    m_grid->setGridSize(QSize(184, 222));
    m_grid->setIconSize(QSize(160, 160));
    m_grid->setWordWrap(true);
    m_grid->setUniformItemSizes(true);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_grid->setItemDelegate(new AlbumCardDelegate(m_grid));
    gridLayout->addWidget(m_grid, 1);
    auto* albumsEmpty = new QLabel(QStringLiteral("还没有专辑\n添加音乐文件夹后，专辑会按封面排在这里。"), gridPage);
    albumsEmpty->setObjectName(QStringLiteral("albumsEmpty"));
    albumsEmpty->setAlignment(Qt::AlignCenter);
    albumsEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().secondary.name()));
    gridLayout->addWidget(albumsEmpty, 1);

    m_detailPage = new QWidget(m_stack);
    auto* detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(28, 18, 18, 8);
    auto* back = new QPushButton(QStringLiteral("‹  专辑"), m_detailPage);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFlat(true);
    back->setCursor(Qt::PointingHandCursor);
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
    play->setObjectName(QStringLiteral("primaryButton"));
    shuffle->setObjectName(QStringLiteral("secondaryButton"));
    play->setCursor(Qt::PointingHandCursor);
    shuffle->setCursor(Qt::PointingHandCursor);
    meta->addWidget(name);
    meta->addWidget(artist);
    meta->addStretch(1);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(play);
    buttons->addWidget(shuffle);
    buttons->addStretch();
    meta->addLayout(buttons);
    auto* metaHost = new QWidget(m_detailPage);
    metaHost->setFixedHeight(180);
    metaHost->setLayout(meta);
    header->setSpacing(20);
    header->addWidget(art, 0, Qt::AlignTop);
    header->addWidget(metaHost, 1, Qt::AlignTop);
    m_detail = new TrackTable(m_detailPage);
    detailLayout->addWidget(back, 0, Qt::AlignLeft);
    detailLayout->addLayout(header);
    detailLayout->addSpacing(18);
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
    const bool empty = m_grid->count() == 0;
    m_grid->setVisible(!empty);
    if (auto* hint = findChild<QLabel*>(QStringLiteral("albumsEmpty"))) {
        hint->setVisible(empty);
    }
}

void AlbumsPage::showArtist(const QString& artistKey)
{
    m_artistFilter = artistKey;
    reload();
}

void AlbumsPage::setPlayingId(qint64 id, bool playing)
{
    m_detail->setPlayingId(id, playing);
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
        art->setPixmap(pixmap.isNull() ? placeholderCover(180) : pixmap.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

class ArtistRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = echo::palette();
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(option.rect.adjusted(4, 2, -4, -2), 6, 6);
        }
        const QRect text = option.rect.adjusted(12, 0, -12, 0);
        painter->setPen(colors.text);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, index.data(Qt::DisplayRole).toString());
        painter->setPen(colors.secondary);
        painter->drawText(text, Qt::AlignRight | Qt::AlignVCenter,
                          QStringLiteral("%1 张专辑").arg(index.data(Qt::UserRole + 1).toInt()));
        painter->restore();
    }
};

ArtistsPage::ArtistsPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(32, 4, 24, 8);
    m_artists = new QListWidget(this);
    m_artists->setObjectName(QStringLiteral("artistList"));
    m_artists->setFixedWidth(220);
    m_artists->setFrameShape(QFrame::NoFrame);
    m_artists->setItemDelegate(new ArtistRowDelegate(m_artists));
    auto* right = new QVBoxLayout();
    m_albums = new QListWidget(this);
    m_albums->setObjectName(QStringLiteral("artistAlbums"));
    m_albums->setViewMode(QListView::IconMode);
    m_albums->setFlow(QListView::LeftToRight);
    m_albums->setWrapping(false);
    m_albums->setMovement(QListView::Static);
    m_albums->setGridSize(QSize(148, 188));
    m_albums->setIconSize(QSize(120, 120));
    m_albums->setFixedHeight(196);
    m_albums->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_albums->setItemDelegate(new AlbumCardDelegate(m_albums));
    m_table = new TrackTable(this);
    auto* artistsEmpty = new QLabel(QStringLiteral("还没有艺人"), this);
    artistsEmpty->setObjectName(QStringLiteral("artistsEmpty"));
    artistsEmpty->setAlignment(Qt::AlignCenter);
    artistsEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().secondary.name()));
    right->addWidget(artistsEmpty, 1);
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
            auto* item = new QListWidgetItem(album.title + QStringLiteral("\n") + album.artist);
            item->setData(Qt::UserRole, album.key);
            item->setSizeHint(QSize(140, 186));
            item->setIcon(QIcon(album.artworkHash.isEmpty() ? placeholderCover(240)
                                                            : m_library->artwork()->pixmap(album.artworkHash, 240)));
            m_albums->addItem(item);
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
        auto* item = new QListWidgetItem(artist.name);
        item->setData(Qt::UserRole, artist.key);
        item->setData(Qt::UserRole + 1, artist.albums);
        item->setSizeHint(QSize(0, 36));
        m_artists->addItem(item);
    }
    const bool empty = m_artists->count() == 0;
    m_artists->setVisible(!empty);
    m_albums->setVisible(!empty);
    m_table->setVisible(!empty);
    if (auto* hint = findChild<QLabel*>(QStringLiteral("artistsEmpty"))) {
        hint->setVisible(empty);
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

void ArtistsPage::setPlayingId(qint64 id, bool playing)
{
    m_table->setPlayingId(id, playing);
}

FoldersPage::FoldersPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(32, 4, 24, 8);
    layout->setSpacing(20);
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("folderTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setMinimumWidth(220);
    m_tree->setMaximumWidth(280);
    auto* right = new QVBoxLayout();
    auto* folderTitle = new QLabel(this);
    folderTitle->setObjectName(QStringLiteral("folderTitle"));
    folderTitle->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 650;"));
    auto* play = new QPushButton(QStringLiteral("播放"), this);
    play->setObjectName(QStringLiteral("playFolder"));
    m_table = new TrackTable(this);
    auto* foldersEmpty = new QLabel(QStringLiteral("还没有文件夹"), this);
    foldersEmpty->setObjectName(QStringLiteral("foldersEmpty"));
    foldersEmpty->setAlignment(Qt::AlignCenter);
    foldersEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().secondary.name()));
    auto* folderHead = new QHBoxLayout();
    folderHead->addWidget(folderTitle, 1);
    folderHead->addWidget(play);
    right->addWidget(foldersEmpty, 1);
    right->addLayout(folderHead);
    right->addWidget(m_table, 1);
    play->setObjectName(QStringLiteral("playFolder"));
    layout->addWidget(m_tree);
    layout->addLayout(right, 1);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (auto* title = findChild<QLabel*>(QStringLiteral("folderTitle"))) {
            title->setText(item->text(0));
        }
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
    QString base = folders.isEmpty() ? QString() : folders.first();
    for (const QString& folder : folders) {
        while (!base.isEmpty() && !folder.startsWith(base)) {
            const int slash = base.lastIndexOf(u'/');
            base = slash > 0 ? base.left(slash) : QString();
        }
    }
    const QString strip = base.isEmpty() ? QString() : base + QLatin1Char('/');
    for (const QString& folder : folders) {
        const QString relative = strip.isEmpty() || !folder.startsWith(strip) ? folder : folder.mid(strip.size());
        const QStringList parts = relative.split(u'/', Qt::SkipEmptyParts);
        QString full = base;
        QTreeWidgetItem* parent = nullptr;
        for (const QString& part : parts) {
            full += QLatin1Char('/') + part;
            if (nodes.contains(full)) {
                parent = nodes.value(full);
                continue;
            }
            auto* item = new QTreeWidgetItem(QStringList {part});
            item->setData(0, Qt::UserRole, full);
            item->setIcon(0, icon(Icon::Folders, echo::palette().secondary, 14));
            if (parent) {
                parent->addChild(item);
            } else {
                m_tree->addTopLevelItem(item);
            }
            nodes.insert(full, item);
            parent = item;
        }
    }
    m_tree->expandAll();
    QTreeWidgetItem* leaf = m_tree->topLevelItem(0);
    while (leaf && leaf->childCount() > 0) {
        leaf = leaf->child(0);
    }
    if (leaf) {
        m_tree->setCurrentItem(leaf);
        if (auto* title = findChild<QLabel*>(QStringLiteral("folderTitle"))) {
            title->setText(leaf->text(0));
        }
        m_table->setTracks(m_library->folderTracks(leaf->data(0, Qt::UserRole).toString(), false));
    }
    const bool empty = folders.isEmpty();
    m_tree->setVisible(!empty);
    m_table->setVisible(!empty);
    if (auto* hint = findChild<QLabel*>(QStringLiteral("foldersEmpty"))) {
        hint->setVisible(empty);
    }
    if (auto* play = findChild<QPushButton*>(QStringLiteral("playFolder"))) {
        play->setVisible(!empty);
    }
    if (auto* title = findChild<QLabel*>(QStringLiteral("folderTitle"))) {
        title->setVisible(!empty);
    }
}

void FoldersPage::setPlayingId(qint64 id, bool playing)
{
    m_table->setPlayingId(id, playing);
}

PlaylistPage::PlaylistPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_table(new TrackTable(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 18, 18, 8);
    auto* title = new QLabel(QStringLiteral("歌单"), this);
    m_emptyFavorites = new QLabel(QStringLiteral("还没有喜欢的歌曲\n点击歌曲旁或播放栏的爱心，收藏喜欢的音乐"), this);
    m_emptyFavorites->setObjectName(QStringLiteral("emptyFavorites"));
    m_emptyFavorites->setAlignment(Qt::AlignCenter);
    m_emptyFavorites->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(echo::palette().secondary.name()));
    m_emptyFavorites->hide();
    layout->addWidget(m_emptyFavorites, 1);
    connect(m_library, &Library::favoritesChanged, this, [this] {
        if (m_id == m_library->favoritesPlaylistId()) reload();
    });
    title->setObjectName(QStringLiteral("playlistTitle"));
    title->hide();
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
    const bool favorites = m_id == m_library->favoritesPlaylistId();
    m_table->setFavoritesContext(favorites);
    m_emptyFavorites->setVisible(favorites && tracks.isEmpty());
    m_table->setVisible(!favorites || !tracks.isEmpty());
    if (favorites) m_table->setTracksPreservingView(tracks);
    else m_table->setTracks(tracks);
}

void PlaylistPage::setPlayingId(qint64 id, bool playing)
{
    m_table->setPlayingId(id, playing);
}

QWidget* resultSection(const QString& title, QWidget* content, QWidget* parent)
{
    auto* section = new QWidget(parent);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSpacing(6);
    auto* label = new QLabel(title, section);
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 650;").arg(echo::palette().secondary.name()));
    layout->addWidget(label);
    layout->addWidget(content);
    return section;
}

QListWidget* resultList(QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setFrameShape(QFrame::NoFrame);
    list->setFocusPolicy(Qt::NoFocus);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    list->setVisible(false);
    return list;
}

SearchPage::SearchPage(Library* library, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 24, 8);
    m_empty = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->addStretch(2);
    m_emptyText = new QLabel(QStringLiteral("没有找到匹配的内容"), m_empty);
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 650;"));
    auto* hint = new QLabel(QStringLiteral("换个关键词，或先把音乐文件夹加进资料库。"), m_empty);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(echo::palette().secondary.name()));
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addWidget(hint);
    emptyLayout->addStretch(3);

    m_body = new QWidget(this);
    auto* body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setAlignment(Qt::AlignTop);
    m_artists = resultList(m_body);
    m_albums = resultList(m_body);
    m_playlists = resultList(m_body);
    m_tracks = new TrackTable(m_body);
    m_artistSection = resultSection(QStringLiteral("艺人"), m_artists, m_body);
    m_albumSection = resultSection(QStringLiteral("专辑"), m_albums, m_body);
    m_playlistSection = resultSection(QStringLiteral("歌单"), m_playlists, m_body);
    m_trackSection = resultSection(QStringLiteral("歌曲"), m_tracks, m_body);
    body->addWidget(m_trackSection);
    body->addWidget(m_artistSection);
    body->addWidget(m_albumSection);
    body->addWidget(m_playlistSection);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("searchScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_body);
    m_tracks->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_empty, 1);
    layout->addWidget(scroll, 1);
    scroll->hide();
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

void SearchPage::showSection(QWidget* section, QListWidget* list)
{
    const bool any = list->count() > 0;
    section->setVisible(any);
    list->setVisible(any);
    if (any) {
        list->setFixedHeight(qMin(list->count(), 6) * 34 + 2);
    }
}

void SearchPage::search(const QString& text)
{
    const auto tracks = m_library->searchTracks(text);
    m_tracks->setTracks(tracks);
    m_trackSection->setVisible(!tracks.isEmpty());
    if (!tracks.isEmpty()) {
        const int header = std::max(28, m_tracks->horizontalHeader()->sizeHint().height());
        m_tracks->setFixedHeight(header + static_cast<int>(tracks.size()) * 40 + 2);
    }
    m_albums->clear();
    for (const Album& album : m_library->searchAlbums(text)) {
        auto* item = new QListWidgetItem(album.title + QStringLiteral(" — ") + album.artist);
        item->setData(Qt::UserRole, album.key);
        item->setSizeHint(QSize(0, 34));
        m_albums->addItem(item);
    }
    m_artists->clear();
    for (const Artist& artist : m_library->searchArtists(text)) {
        auto* item = new QListWidgetItem(artist.name);
        item->setData(Qt::UserRole, artist.key);
        item->setSizeHint(QSize(0, 34));
        m_artists->addItem(item);
    }
    m_playlists->clear();
    for (const Playlist& playlist : m_library->searchPlaylists(text)) {
        auto* item = new QListWidgetItem(playlist.name);
        item->setData(Qt::UserRole, playlist.id);
        item->setSizeHint(QSize(0, 34));
        m_playlists->addItem(item);
    }
    showSection(m_artistSection, m_artists);
    showSection(m_albumSection, m_albums);
    showSection(m_playlistSection, m_playlists);
    const bool any = !tracks.isEmpty() || m_artists->count() > 0 || m_albums->count() > 0 || m_playlists->count() > 0;
    m_empty->setVisible(!any);
    if (auto* scroll = findChild<QScrollArea*>(QStringLiteral("searchScroll"))) {
        scroll->setVisible(any);
    }
    m_body->setVisible(true);
    m_emptyText->setText(QStringLiteral("没有找到「%1」").arg(text));
}

void SearchPage::setPlayingId(qint64 id, bool playing)
{
    m_tracks->setPlayingId(id, playing);
}

} // namespace echo
