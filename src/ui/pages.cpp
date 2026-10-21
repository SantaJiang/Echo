#include "ui/pages.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHash>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QScrollBar>
#include "platform/platform.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

TrackListPage::TrackListPage(Library* library, Source source, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_table(new TrackTable(this))
    , m_source(source)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 4, 24, 8);
    auto* actions = new QHBoxLayout;
    m_subtitle = new QLabel(this);
    m_subtitle->setStyleSheet(QStringLiteral("color: %1;").arg(echo::palette().secondary.name()));
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subtitle->setWordWrap(true);
    auto* play = new QPushButton(QStringLiteral("播放全部"), this);
    play->setObjectName(QStringLiteral("playAll"));
    auto* shuffle = new QPushButton(QStringLiteral("随机播放"), this);
    actions->addWidget(m_subtitle, 1);
    actions->addWidget(play);
    actions->addWidget(shuffle);
    layout->addLayout(actions);
    m_empty = new QLabel(this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setObjectName(
        source == Source::Playlist ? QStringLiteral("emptyFavorites") : QStringLiteral("trackListEmpty"));
    m_empty->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().secondary.name()));
    m_add = new QPushButton(QStringLiteral("添加音乐文件夹"), this);
    layout->addWidget(m_empty, 1);
    layout->addWidget(m_add, 0, Qt::AlignHCenter);
    layout->addWidget(m_table, 1);
    m_table->setPlaylistContext(source == Source::Playlist);
    connect(m_add, &QPushButton::clicked, this, &TrackListPage::addFolderRequested);
    connect(play, &QPushButton::clicked, this, [this] { emit playAllRequested(m_table->trackModel()->ids(), false); });
    connect(
        shuffle, &QPushButton::clicked, this, [this] { emit playAllRequested(m_table->trackModel()->ids(), true); });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section == 0) {
            m_sort = 0;
            m_desc = false;
            m_library->setSetting(viewKey() + QStringLiteral("_sort"), QStringLiteral("0"));
            m_library->setSetting(viewKey() + QStringLiteral("_desc"), QStringLiteral("0"));
            reload();
            return;
        }
        if (m_sort == section)
            m_desc = !m_desc;
        else {
            m_sort = section;
            m_desc = false;
        }
        m_library->setSetting(viewKey() + QStringLiteral("_sort"), QString::number(m_sort));
        m_library->setSetting(viewKey() + QStringLiteral("_desc"), m_desc ? QStringLiteral("1") : QStringLiteral("0"));
        reload();
    });
    m_sort = m_library->setting(viewKey() + QStringLiteral("_sort"), QStringLiteral("0")).toInt();
    m_desc = m_library->setting(viewKey() + QStringLiteral("_desc"), QStringLiteral("0")) == QLatin1String("1");
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!m_changingSource)
            saveView();
    });
    connect(m_library, &Library::favoritesChanged, this, [this] {
        if (m_source == Source::Playlist && m_id == m_library->favoritesPlaylistId())
            reload();
    });
}

QString TrackListPage::viewKey() const
{
    return QStringLiteral("list_%1_%2").arg(static_cast<int>(m_source)).arg(m_id);
}

void TrackListPage::saveView()
{
    View view;
    view.sort = m_sort;
    view.desc = m_desc;
    view.scroll = m_table->verticalScrollBar()->value();
    for (const auto& row : m_table->selectionModel()->selectedRows()) {
        const auto track = m_table->trackModel()->trackAt(row.row());
        view.selected << (m_source == Source::Playlist && m_id == m_library->favoritesPlaylistId() ? track.id
                : track.listEntryId                                                                ? track.listEntryId
                                                                                                   : track.id);
    }
    m_views[m_id] = view;
}

void TrackListPage::setSource(qint64 id)
{
    if (m_id == id) {
        reload();
        return;
    }
    saveView();
    m_changingSource = true;
    m_id = id;
    m_sort = m_library->setting(viewKey() + QStringLiteral("_sort"), QStringLiteral("0")).toInt();
    m_desc = m_library->setting(viewKey() + QStringLiteral("_desc"), QStringLiteral("0")) == QLatin1String("1");
    m_table->setTracks({ });
    reload();
    const auto view = m_views.value(id);
    for (int row = 0; row < m_table->trackModel()->rowCount(); ++row) {
        const auto item = m_table->trackModel()->trackAt(row);
        if (view.selected.contains(m_source == Source::Playlist && m_id == m_library->favoritesPlaylistId() ? item.id
                    : item.listEntryId ? item.listEntryId
                                       : item.id))
            m_table->selectionModel()->select(
                m_table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_table->verticalScrollBar()->setValue(view.scroll);
    m_changingSource = false;
}

void TrackListPage::reload()
{
    QVector<Track> tracks;
    QString extra;
    if (m_source == Source::AllSongs) {
        m_title = QStringLiteral("歌曲");
        tracks = m_library->allTracks(QStringLiteral("album"), false);
        m_empty->setText(QStringLiteral("资料库还是空的\n添加音乐文件夹后，只收录该文件夹当前层的音乐。"));
    } else if (m_source == Source::Folder) {
        const auto source = m_library->root(m_id);
        m_title = source.name;
        extra = source.path;
        if (!source.message.isEmpty())
            extra += QStringLiteral(" · ") + source.message;
        tracks = m_library->folderTracks(m_id);
        m_empty->setText(QStringLiteral("当前层没有音乐\n子目录需要单独添加为音乐文件夹。"));
    } else {
        m_title = QStringLiteral("播放列表");
        for (const auto& playlist : m_library->playlists())
            if (playlist.id == m_id)
                m_title = playlist.name;
        tracks = m_library->playlistTracks(m_id);
        const bool favorites = m_id == m_library->favoritesPlaylistId();
        m_table->setFavoritesContext(favorites);
        m_empty->setText(favorites ? QStringLiteral("还没有喜欢的歌曲\n点击歌曲旁或播放栏的爱心，收藏喜欢的音乐")
                                   : QStringLiteral("播放列表还是空的\n可以从歌曲或文件夹添加音乐。"));
    }
    if (m_sort)
        std::stable_sort(tracks.begin(), tracks.end(), [this](const Track& a, const Track& b) {
            int order = 0;
            if (m_sort == 4)
                order = a.durationMs < b.durationMs ? -1 : a.durationMs > b.durationMs ? 1 : 0;
            else
                order = QString::localeAwareCompare(m_sort == 2 ? a.displayArtist()
                        : m_sort == 3                           ? a.displayAlbum()
                                                                : a.displayTitle(),
                    m_sort == 2       ? b.displayArtist()
                        : m_sort == 3 ? b.displayAlbum()
                                      : b.displayTitle());
            return m_desc ? order > 0 : order < 0;
        });
    int available = 0;
    for (const auto& item : tracks)
        if (!item.missing)
            ++available;
    m_subtitle->setText((extra.isEmpty() ? QString() : extra + QStringLiteral("\n"))
        + QStringLiteral("%1 首歌曲 · %2 首可播放").arg(tracks.size()).arg(available));
    const bool empty = tracks.isEmpty();
    m_empty->setVisible(empty);
    m_table->setVisible(!empty);
    m_add->setVisible(empty && m_source == Source::AllSongs);
    const bool changing = m_changingSource;
    m_changingSource = true;
    m_table->setTracksPreservingView(std::move(tracks));
    m_changingSource = changing;
    emit titleChanged(m_title);
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
    const auto selected = m_grid->currentItem() ? m_grid->currentItem()->data(Qt::UserRole).toString() : QString();
    const int scroll = m_grid->verticalScrollBar()->value();
    m_grid->clear();
    for (const Album& album : m_library->albums(m_artistFilter)) {
        fillAlbumItem(m_grid, album, m_library->artwork());
    }
    for (int i = 0; i < m_grid->count(); ++i)
        if (m_grid->item(i)->data(Qt::UserRole).toString() == selected)
            m_grid->setCurrentRow(i);
    m_grid->verticalScrollBar()->setValue(scroll);
    if (m_stack->currentIndex() == 1)
        showAlbum(m_detail->property("albumKey").toString());
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
    m_detail->setTracksPreservingView(tracks);
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
        m_table->setTracksPreservingView(m_library->artistTracks(key));
    });
    connect(m_albums, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit albumPlayRequested(item->data(Qt::UserRole).toString(), false);
    });
}

void ArtistsPage::reload()
{
    const auto selected
        = m_artists->currentItem() ? m_artists->currentItem()->data(Qt::UserRole).toString() : QString();
    const int scroll = m_artists->verticalScrollBar()->value();
    m_artists->clear();
    for (const Artist& artist : m_library->artists()) {
        auto* item = new QListWidgetItem(artist.name);
        item->setData(Qt::UserRole, artist.key);
        item->setData(Qt::UserRole + 1, artist.albums);
        item->setSizeHint(QSize(0, 36));
        m_artists->addItem(item);
    }
    for (int i = 0; i < m_artists->count(); ++i)
        if (m_artists->item(i)->data(Qt::UserRole).toString() == selected) {
            m_artists->setCurrentRow(i);
            break;
        }
    m_artists->verticalScrollBar()->setValue(scroll);
    const bool empty = m_artists->count() == 0;
    m_artists->setVisible(!empty);
    m_albums->setVisible(!empty);
    m_table->setVisible(!empty);
    if (auto* hint = findChild<QLabel*>(QStringLiteral("artistsEmpty"))) {
        hint->setVisible(empty);
    }
    if (m_artists->count() > 0 && m_artists->currentRow() < 0) {
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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);
    auto* listing = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listing);
    listLayout->setContentsMargins(32, 4, 24, 8);
    auto* add = new QPushButton(QStringLiteral("添加文件夹…"), listing);
    add->setObjectName(QStringLiteral("addMusicFolder"));
    listLayout->addWidget(add, 0, Qt::AlignRight);
    m_empty = new QLabel(QStringLiteral("还没有文件夹\n手动添加音乐文件夹，只扫描当前层。"), listing);
    m_empty->setObjectName(QStringLiteral("foldersEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    listLayout->addWidget(m_empty, 1);
    m_tree = new QTreeWidget(listing);
    m_tree->setObjectName(QStringLiteral("folderTree"));
    m_tree->setHeaderLabels(
        { QStringLiteral("名称"), QStringLiteral("路径"), QStringLiteral("歌曲"), QStringLiteral("状态") });
    m_tree->setRootIsDecorated(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 180);
    m_tree->setColumnWidth(2, 120);
    m_tree->setColumnWidth(3, 130);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    listLayout->addWidget(m_tree, 1);
    m_stack->addWidget(listing);
    auto* detail = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    auto* back = new QPushButton(QStringLiteral("‹ 文件夹"), detail);
    back->setObjectName(QStringLiteral("backToFolders"));
    detailLayout->addWidget(back, 0, Qt::AlignLeft);
    m_detail = new TrackListPage(library, TrackListPage::Source::Folder, detail);
    detailLayout->addWidget(m_detail, 1);
    m_stack->addWidget(detail);
    connect(back, &QPushButton::clicked, this, &FoldersPage::showFolders);
    connect(add, &QPushButton::clicked, this, &FoldersPage::addFolderRequested);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int) { openFolder(item->data(0, Qt::UserRole).toLongLong()); });
    connect(m_detail, &TrackListPage::titleChanged, this, [this](const QString& title) {
        if (m_stack->currentIndex() == 1)
            emit titleChanged(title);
    });
    connect(m_library, &Library::scanStarted, this, [this](qint64) { reload(); });
    connect(m_library, &Library::scanFinished, this, &FoldersPage::reload);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = m_tree->itemAt(point);
        if (!item)
            return;
        const auto id = item->data(0, Qt::UserRole).toLongLong();
        const auto source = m_library->root(id);
        QMenu menu(this);
        menu.addAction(QStringLiteral("创建播放列表…"), this, [this, id, source] {
            bool ok = false;
            const auto name = QInputDialog::getText(
                this, QStringLiteral("创建播放列表"), QStringLiteral("名称"), QLineEdit::Normal, source.name, &ok)
                                  .trimmed();
            if (!ok || name.isEmpty())
                return;
            QVector<qint64> ids;
            if (m_detail->sourceId() == id)
                ids = m_detail->table()->trackModel()->ids();
            else
                for (const auto& track : m_library->folderTracks(id))
                    ids << track.id;
            if (ids.isEmpty()) {
                QMessageBox::information(
                    this, QStringLiteral("无法创建"), QStringLiteral("当前文件夹还没有已知歌曲。"));
                return;
            }
            const auto playlist = m_library->createPlaylistFromFolder(id, name, ids);
            if (playlist)
                emit playlistCreated(playlist);
            else
                QMessageBox::warning(this, QStringLiteral("创建失败"), QStringLiteral("无法保存播放列表。"));
        });
        menu.addAction(QStringLiteral("重新扫描"), this, [this, id] { m_library->refreshRoot(id); });
        menu.addAction(QStringLiteral("在 Finder 中显示"), this, [source] { showInFileManager(source.path); });
        menu.addAction(QStringLiteral("重新定位…"), this, [this, id] {
            const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("重新定位文件夹"));
            if (path.isEmpty())
                return;
            QString error;
            if (!m_library->relocateRoot(id, path, &error))
                QMessageBox::warning(this, QStringLiteral("定位失败"), error);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("移除文件夹…"), this, [this, id] {
            if (QMessageBox::question(this, QStringLiteral("移除文件夹"),
                    QStringLiteral("取消登记这个音乐来源？\n歌曲将退出全局资料库，已有歌单、收藏和队列引用会保留，磁盘"
                                   "文件不会删除。"))
                == QMessageBox::Yes)
                m_library->removeRoot(id);
        });
        menu.exec(m_tree->viewport()->mapToGlobal(point));
    });
}

QString FoldersPage::title() const
{
    return m_stack->currentIndex() == 1 ? m_detail->title() : QStringLiteral("文件夹");
}
void FoldersPage::showFolders()
{
    m_stack->setCurrentIndex(0);
    emit titleChanged(title());
}
void FoldersPage::selectFolder(qint64 id)
{
    reload();
    showFolders();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == id) {
            m_tree->setCurrentItem(m_tree->topLevelItem(i));
            m_tree->scrollToItem(m_tree->topLevelItem(i));
            break;
        }
}
void FoldersPage::openFolder(qint64 id)
{
    m_detail->setSource(id);
    m_stack->setCurrentIndex(1);
    emit titleChanged(title());
    m_library->refreshRoot(id);
}
void FoldersPage::reload()
{
    const qint64 selected = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toLongLong() : 0;
    const int scroll = m_tree->verticalScrollBar()->value();
    const auto roots = m_library->roots();
    QSet<qint64> retained;
    int position = 0;
    for (const auto& source : roots) {
        retained.insert(source.id);
        QTreeWidgetItem* item = nullptr;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            if (m_tree->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == source.id)
                item = m_tree->topLevelItem(i);
        if (!item) {
            item = new QTreeWidgetItem(m_tree);
            item->setData(0, Qt::UserRole, source.id);
        }
        const int previousPosition = m_tree->indexOfTopLevelItem(item);
        if (previousPosition != position)
            m_tree->insertTopLevelItem(position, m_tree->takeTopLevelItem(previousPosition));
        ++position;
        const int available = source.availableTracks;
        const QHash<QString, QString> statuses { { "available", "可用" }, { "missing", "找不到目录" },
            { "offline", "磁盘未连接" }, { "permission", "无访问权限" }, { "unavailable", "暂时无法访问" } };
        item->setSizeHint(0, QSize(0, 42));
        item->setText(0, source.name);
        item->setText(1, source.path);
        item->setText(2, QStringLiteral("%1 / %2 可播放").arg(available).arg(source.tracks));
        item->setText(3,
            m_library->scanningRoot() == source.id ? QStringLiteral("扫描中…")
                                                   : statuses.value(source.status, source.status));
        item->setToolTip(1, source.path);
        item->setToolTip(3, source.message);
        item->setIcon(0, icon(Icon::Folders, echo::palette().secondary, 16));
        if (source.id == selected)
            m_tree->setCurrentItem(item);
    }
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i)
        if (!retained.contains(m_tree->topLevelItem(i)->data(0, Qt::UserRole).toLongLong()))
            delete m_tree->takeTopLevelItem(i);
    m_tree->verticalScrollBar()->setValue(scroll);
    m_empty->setVisible(roots.isEmpty());
    m_tree->setVisible(!roots.isEmpty());
    if (m_stack->currentIndex() == 1) {
        if (!retained.contains(m_detail->sourceId()))
            showFolders();
        else
            m_detail->reload();
    }
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
    m_tracks->setTracksPreservingView(tracks);
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
