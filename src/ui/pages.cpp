#include "ui/dialogs.h"
#include "ui/pages.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QUrl>
#include <QSignalBlocker>
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

class FolderTree : public QTreeWidget {
public:
    explicit FolderTree(QWidget* parent) : QTreeWidget(parent) { setProperty("hoverRow", -1); }
protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int row = indexAt(event->position().toPoint()).row();
        if (property("hoverRow").toInt() != row) {
            setProperty("hoverRow", row);
            viewport()->update();
        }
        QTreeWidget::mouseMoveEvent(event);
    }
    bool viewportEvent(QEvent* event) override
    {
        if (event->type() == QEvent::Leave) {
            setProperty("hoverRow", -1);
            viewport()->update();
        }
        return QTreeWidget::viewportEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        setProperty("hoverRow", -1);
        viewport()->update();
        QTreeWidget::leaveEvent(event);
    }
};

class FolderRowDelegate : public QStyledItemDelegate {
public:
    explicit FolderRowDelegate(QTreeWidget* tree) : QStyledItemDelegate(tree), m_tree(tree) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = m_tree->property("hoverRow").toInt() == index.row();
        if (selected || hover) {
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? echo::palette().selected : echo::palette().hover);
            painter->drawRoundedRect(QRectF(4, option.rect.top() + 2,
                m_tree->viewport()->width() - 8, option.rect.height() - 4), 8, 8);
        }
        auto content = option;
        content.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter, content, index);
        painter->restore();
    }
private:
    QTreeWidget* m_tree;
};

class PathLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setElideMode(Qt::TextElideMode mode) { m_elide = mode; }
    void setPath(const QString& path)
    {
        setTextFormat(Qt::PlainText);
        m_path = path;
        setToolTip(path);
        setText(fontMetrics().elidedText(path, m_elide, width()));
        setVisible(!path.isEmpty());
    }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(m_path, m_elide, width()));
    }
private:
    QString m_path;
    Qt::TextElideMode m_elide = Qt::ElideMiddle;
};

class ArtistLinkLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setArtist(const QString& name, const QString& key)
    {
        m_name = name;
        m_key = key;
        setToolTip(name);
        refresh();
    }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        refresh();
    }
private:
    void refresh()
    {
        const auto text = fontMetrics().elidedText(m_name, Qt::ElideRight, width());
        setText(QStringLiteral(R"(<a href="%1" style="color:%2; text-decoration:none;">%3</a>)")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(m_key)), echo::palette().accent.name(), text.toHtmlEscaped()));
    }
    QString m_name;
    QString m_key;
};

QString actionStyle(bool primary)
{
    const auto colors = echo::palette();
    return QStringLiteral("QPushButton { background: %1; color: %2; border: none; border-radius: 7px; "
                          "padding: 0 14px; font-size: 12px; font-weight: 600; }"
                          "QPushButton:hover { background: %3; }"
                          "QPushButton:disabled { color: %4; background: %5; }")
        .arg(primary ? colors.accent.name() : colors.hover.name(),
            primary ? QStringLiteral("#FFFFFF") : colors.text.name(),
            primary ? QStringLiteral("#FF4560") : colors.selected.name(),
            colors.secondary.name(), colors.sidebar.name());
}

QPixmap roundedArtwork(const QPixmap& source, int side)
{
    QPixmap result(side * 2, side * 2);
    result.setDevicePixelRatio(2);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), 10, 10);
    painter.setClipPath(clip);
    const int crop = qMin(source.width(), source.height());
    painter.drawPixmap(QRect(0, 0, side, side), source,
        QRect((source.width() - crop) / 2, (source.height() - crop) / 2, crop, crop));
    return result;
}

class AlbumCardDelegate : public QStyledItemDelegate {
public:
    explicit AlbumCardDelegate(ArtworkCache* cache, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_cache(cache)
    {
        connect(cache, &ArtworkCache::ready, this, [parent] { if (auto* view = qobject_cast<QAbstractItemView*>(parent)) view->viewport()->update(); });
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const Palette colors = echo::palette();
        const int coverSide = qMin(160, qMin(option.rect.width() - 16, option.rect.height() - 56));
        const QRect cover(option.rect.left() + 8, option.rect.top() + 6, coverSide, coverSide);
        if (option.state & QStyle::State_Selected || option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(QRectF(option.rect.adjusted(2, 0, -2, -2)), 10, 10);
        }
        QPixmap pixmap = m_cache->pixmap(index.data(Qt::UserRole + 3).toString(), 320);
        if (pixmap.isNull()) {
            static const QPixmap placeholder = placeholderCover(160);
            pixmap = placeholder;
        }
        QPainterPath clip;
        clip.addRoundedRect(QRectF(cover), 8, 8);
        painter->setClipPath(clip);
        if (!pixmap.isNull()) {
            const int crop = qMin(pixmap.width(), pixmap.height());
            painter->drawPixmap(cover, pixmap,
                QRect((pixmap.width() - crop) / 2, (pixmap.height() - crop) / 2, crop, crop));
        } else {
            painter->fillRect(cover, colors.sidebar);
        }
        painter->setClipping(false);
        const QStringList lines = index.data(Qt::DisplayRole).toString().split(u'\n');
        QFont textFont = option.font;
        textFont.setPixelSize(12);
        textFont.setWeight(QFont::Medium);
        painter->setFont(textFont);
        const QRect title(cover.left(), cover.bottom() + 8, cover.width(), 18);
        const QRect artist(cover.left(), title.bottom(), cover.width(), 16);
        painter->setPen(colors.text);
        painter->drawText(title, Qt::AlignLeft | Qt::AlignVCenter, painter->fontMetrics().elidedText(lines.value(0), Qt::ElideRight, title.width()));
        painter->setPen(colors.secondary);
        textFont.setPixelSize(11);
        textFont.setWeight(QFont::Normal);
        painter->setFont(textFont);
        painter->drawText(artist, Qt::AlignLeft | Qt::AlignVCenter, painter->fontMetrics().elidedText(lines.value(1), Qt::ElideRight, artist.width()));
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(176, 214);
    }
private:
    ArtworkCache* m_cache;
};

void updateAlbumItem(QListWidgetItem* item, const Album& album, QSize size)
{
    item->setText(album.title + QStringLiteral("\n") + album.artist);
    item->setData(Qt::UserRole, album.key);
    item->setData(Qt::UserRole + 3, album.artworkHash);
    item->setSizeHint(size);
    item->setToolTip(album.title + QStringLiteral("\n") + album.artist + QStringLiteral(" · %1 首歌曲").arg(album.tracks));
}

void syncAlbumItems(QListWidget* list, const QVector<Album>& albums, QSize size = QSize(176, 214))
{
    QHash<QString, QListWidgetItem*> existing;
    for (int i = 0; i < list->count(); ++i) existing.insert(list->item(i)->data(Qt::UserRole).toString(), list->item(i));
    for (int row = 0; row < albums.size(); ++row) {
        auto* item = existing.take(albums[row].key);
        if (!item) { item = new QListWidgetItem; list->insertItem(row, item); }
        else if (list->row(item) != row) list->insertItem(row, list->takeItem(list->row(item)));
        updateAlbumItem(item, albums[row], size);
    }
    for (auto* item : existing) delete list->takeItem(list->row(item));
}

} // namespace

TrackListPage::TrackListPage(Library* library, Source source, QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_table(new TrackTable(this))
    , m_source(source)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ContentLeftMargin, 4, ContentRightMargin, 8);
    layout->setSpacing(12);
    auto* path = new PathLabel(this);
    m_path = path;
    path->setObjectName(QStringLiteral("trackListPath"));
    path->setMinimumWidth(0);
    path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    path->setFixedHeight(20);
    path->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(echo::palette().secondary.name()));
    path->hide();
    layout->addWidget(path);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->setContentsMargins(0, 0, 0, 4);
    m_subtitle = new QLabel(this);
    m_subtitle->setStyleSheet(QStringLiteral("color: %1;").arg(echo::palette().secondary.name()));
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subtitle->setWordWrap(false);
    m_subtitle->setObjectName(QStringLiteral("trackListCount"));
    auto* play = new QPushButton(QStringLiteral("播放全部"), this);
    play->setObjectName(QStringLiteral("playAll"));
    auto* shuffle = new QPushButton(QStringLiteral("随机播放"), this);
    play->setIcon(icon(Icon::Play, Qt::white, 12));
    shuffle->setIcon(icon(Icon::Shuffle, echo::palette().text, 14));
    for (auto* button : {play, shuffle}) {
        button->setFixedHeight(32);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(actionStyle(button == play));
    }
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
        if (m_source == Source::Playlist && m_id == m_library->favoritesPlaylistId()) {
            if (isVisible()) reload();
            else setProperty("echoDirty", true);
        }
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
    static_cast<PathLabel*>(m_path)->setPath(extra);
    m_subtitle->setText(available == tracks.size()
        ? QStringLiteral("%1 首歌曲").arg(tracks.size())
        : QStringLiteral("%1 首歌曲 · %2 首可播放 · %3 首暂不可用").arg(tracks.size()).arg(available).arg(tracks.size() - available));
    for (auto* button : findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly))
        if (button != m_add)
            button->setEnabled(available > 0);
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
    gridLayout->setContentsMargins(ContentLeftMargin, 4, ContentRightMargin, 8);
    m_grid = new QListWidget(gridPage);
    m_grid->setObjectName(QStringLiteral("albumGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setFlow(QListView::LeftToRight);
    m_grid->setWrapping(true);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setSpacing(0);
    m_grid->setMouseTracking(true);
    m_grid->setGridSize(QSize(184, 222));
    m_grid->setIconSize(QSize(160, 160));
    m_grid->setWordWrap(true);
    m_grid->setUniformItemSizes(true);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_grid->setItemDelegate(new AlbumCardDelegate(m_library->artwork(), m_grid));
    gridLayout->addWidget(m_grid, 1);
    auto* albumsEmpty = new QLabel(QStringLiteral("还没有专辑\n添加音乐文件夹后，专辑会按封面排在这里。"), gridPage);
    albumsEmpty->setObjectName(QStringLiteral("albumsEmpty"));
    albumsEmpty->setAlignment(Qt::AlignCenter);
    albumsEmpty->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().secondary.name()));
    gridLayout->addWidget(albumsEmpty, 1);

    m_detailPage = new QWidget(m_stack);
    auto* detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(ContentLeftMargin, 4, ContentRightMargin, 8);
    auto* back = new QPushButton(QStringLiteral("‹  专辑"), m_detailPage);
    back->setObjectName(QStringLiteral("backButton"));
    back->setFlat(true);
    back->setCursor(Qt::PointingHandCursor);
    auto* header = new QHBoxLayout();
    auto* art = new QLabel(m_detailPage);
    art->setObjectName(QStringLiteral("albumArt"));
    art->setFixedSize(144, 144);
    art->setScaledContents(true);
    auto* meta = new QVBoxLayout();
    auto* name = new PathLabel(m_detailPage);
    name->setElideMode(Qt::ElideRight);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    name->setFixedHeight(34);
    name->setObjectName(QStringLiteral("albumName"));
    name->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: 700;"));
    auto* artist = new ArtistLinkLabel(m_detailPage);
    artist->setObjectName(QStringLiteral("albumArtist"));
    artist->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;").arg(echo::palette().accent.name()));
    auto* play = new QPushButton(QStringLiteral("播放"), m_detailPage);
    auto* shuffle = new QPushButton(QStringLiteral("随机播放"), m_detailPage);
    play->setObjectName(QStringLiteral("primaryButton"));
    shuffle->setObjectName(QStringLiteral("secondaryButton"));
    for (auto* button : {play, shuffle}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(32);
        button->setStyleSheet(actionStyle(button == play));
        button->setIcon(icon(button == play ? Icon::Play : Icon::Shuffle,
            button == play ? QColor(Qt::white) : echo::palette().text, 13));
    }
    meta->setContentsMargins(0, 0, 0, 0);
    meta->setSpacing(6);
    meta->addWidget(name);
    artist->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    artist->setFixedHeight(22);
    meta->addWidget(artist);
    auto* stats = new QLabel(m_detailPage);
    stats->setObjectName(QStringLiteral("albumStats"));
    stats->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(echo::palette().secondary.name()));
    meta->addWidget(stats);
    meta->addStretch(1);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(play);
    buttons->addWidget(shuffle);
    buttons->addStretch();
    meta->addLayout(buttons);
    auto* metaHost = new QWidget(m_detailPage);
    metaHost->setFixedHeight(144);
    metaHost->setLayout(meta);
    header->setSpacing(20);
    header->addWidget(art, 0, Qt::AlignTop);
    header->addWidget(metaHost, 1, Qt::AlignTop);
    m_detail = new TrackTable(m_detailPage);
    auto* backRow = new QHBoxLayout;
    backRow->setContentsMargins(0, 0, 0, 0);
    back->setFlat(true);
    back->setFixedHeight(26);
    back->setCursor(Qt::PointingHandCursor);
    back->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; color: %1; padding: 0; font-size: 12px; }"
        "QPushButton:hover { color: %2; }").arg(echo::palette().secondary.name(), echo::palette().text.name()));
    backRow->addWidget(back, 0, Qt::AlignLeft);
    backRow->addStretch();
    detailLayout->setSpacing(12);
    detailLayout->addLayout(backRow);
    detailLayout->addLayout(header);
    detailLayout->addSpacing(4);
    detailLayout->addWidget(m_detail, 1);

    m_stack->addWidget(gridPage);
    m_stack->addWidget(m_detailPage);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { openAlbum(item->data(Qt::UserRole).toString()); });
    connect(m_grid, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        openAlbum(item->data(Qt::UserRole).toString());
    });
    connect(back, &QPushButton::clicked, this, [this] { m_stack->setCurrentIndex(0); });
    connect(m_library->artwork(), &ArtworkCache::ready, this, [this](const QString&, int size) {
        if (size == 360 && m_stack->currentIndex() == 1) showAlbum(m_detail->property("albumKey").toString());
    });
    connect(play, &QPushButton::clicked, this, [this] {
        emit albumPlayRequested(m_detail->property("albumKey").toString(), false);
    });
    connect(shuffle, &QPushButton::clicked, this, [this] {
        emit albumPlayRequested(m_detail->property("albumKey").toString(), true);
    });
    connect(artist, &QLabel::linkActivated, this, [this](const QString& key) { emit artistRequested(QUrl::fromPercentEncoding(key.toUtf8())); });
}

void AlbumsPage::reload()
{
    const auto selected = m_grid->currentItem() ? m_grid->currentItem()->data(Qt::UserRole).toString() : QString();
    const int scroll = m_grid->verticalScrollBar()->value();
    syncAlbumItems(m_grid, m_library->albums(m_artistFilter));
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
    if (m_detail->property("albumKey").toString() == key)
        m_detail->setTracksPreservingView(tracks);
    else
        m_detail->setTracks(tracks);
    m_detail->setProperty("albumKey", key);
    auto* name = m_detailPage->findChild<QLabel*>(QStringLiteral("albumName"));
    auto* artist = m_detailPage->findChild<QLabel*>(QStringLiteral("albumArtist"));
    auto* art = m_detailPage->findChild<QLabel*>(QStringLiteral("albumArt"));
    int playable = 0;
    qint64 duration = 0;
    for (const auto& track : tracks) {
        duration += track.durationMs;
        playable += !track.missing;
    }
    auto* stats = m_detailPage->findChild<QLabel*>(QStringLiteral("albumStats"));
    stats->setText(QStringLiteral("%1 首歌曲 · %2 · %3 首可播放").arg(tracks.size()).arg(formatDuration(duration)).arg(playable));
    for (auto* button : m_detailPage->findChildren<QPushButton*>())
        if (button->objectName() != QLatin1String("backButton"))
            button->setEnabled(playable > 0);
    if (tracks.isEmpty()) {
        static_cast<PathLabel*>(name)->setPath(QStringLiteral("专辑暂不可用"));
        static_cast<ArtistLinkLabel*>(artist)->setArtist({}, {});
        art->setPixmap(placeholderCover(144));
    }
    if (!tracks.isEmpty()) {
        static_cast<PathLabel*>(name)->setPath(tracks.first().displayAlbum());
        artist->setTextFormat(Qt::RichText);
        artist->setTextInteractionFlags(Qt::TextBrowserInteraction);
        artist->setOpenExternalLinks(false);
        static_cast<ArtistLinkLabel*>(artist)->setArtist(tracks.first().displayAlbumArtist(), tracks.first().artistKey);
        QString artworkHash;
        for (const auto& track : tracks)
            if (!track.artworkHash.isEmpty()) {
                artworkHash = track.artworkHash;
                break;
            }
        const auto pixmap = m_library->artwork()->pixmap(artworkHash, 360);
        art->setPixmap(roundedArtwork(pixmap.isNull() ? placeholderCover(288) : pixmap, 144));
    }
}

class ArtistRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto colors = echo::palette();
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state & QStyle::State_Selected ? colors.selected : colors.hover);
            painter->drawRoundedRect(option.rect.adjusted(0, 2, -2, -2), 8, 8);
        }
        const QRect avatar(option.rect.left() + 10, option.rect.center().y() - 16, 32, 32);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.hover);
        painter->drawEllipse(avatar);
        icon(Icon::Artists, colors.secondary, 16).paint(painter, avatar.adjusted(8, 8, -8, -8));
        QFont font = option.font;
        font.setPixelSize(13);
        font.setWeight(QFont::Medium);
        painter->setFont(font);
        const QRect name(option.rect.left() + 52, option.rect.top() + 9, option.rect.width() - 64, 18);
        painter->setPen(colors.text);
        painter->drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
            painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, name.width()));
        font.setPixelSize(11);
        font.setWeight(QFont::Normal);
        painter->setFont(font);
        painter->setPen(colors.secondary);
        painter->drawText(name.translated(0, 19), Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("%1 张专辑 · %2 首歌曲").arg(index.data(Qt::UserRole + 1).toInt()).arg(index.data(Qt::UserRole + 2).toInt()));
        painter->restore();
    }
};

ArtistsPage::ArtistsPage(Library* library, QWidget* parent)
    : QWidget(parent), m_library(library)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(ContentLeftMargin, 4, ContentRightMargin, 8);
    layout->setSpacing(20);
    m_artists = new QListWidget(this);
    m_artists->setObjectName(QStringLiteral("artistList"));
    m_artists->setFixedWidth(200);
    m_artists->setFrameShape(QFrame::NoFrame);
    m_artists->setMouseTracking(true);
    m_artists->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_artists->setItemDelegate(new ArtistRowDelegate(m_artists));
    m_details = new QWidget(this);
    auto* right = new QVBoxLayout(m_details);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(10);
    auto* header = new QHBoxLayout;
    auto* info = new QVBoxLayout;
    info->setSpacing(4);
    auto* name = new PathLabel(m_details);
    name->setElideMode(Qt::ElideRight);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    name->setFixedHeight(30);
    name->setObjectName(QStringLiteral("artistName"));
    name->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 600;"));
    m_name = name;
    m_summary = new QLabel(m_details);
    m_summary->setObjectName(QStringLiteral("artistStats"));
    m_summary->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(echo::palette().secondary.name()));
    info->addWidget(name);
    info->addWidget(m_summary);
    header->addLayout(info, 1);
    m_play = new QPushButton(QStringLiteral("播放全部"), m_details);
    m_shuffle = new QPushButton(QStringLiteral("随机播放"), m_details);
    m_play->setObjectName(QStringLiteral("playArtist"));
    m_shuffle->setObjectName(QStringLiteral("shuffleArtist"));
    for (auto* button : {m_play, m_shuffle}) {
        button->setFixedHeight(32);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(actionStyle(button == m_play));
        button->setIcon(icon(button == m_play ? Icon::Play : Icon::Shuffle,
            button == m_play ? QColor(Qt::white) : echo::palette().text, 13));
        header->addWidget(button, 0, Qt::AlignVCenter);
    }
    right->addLayout(header);
    m_albums = new QListWidget(m_details);
    m_albums->setObjectName(QStringLiteral("artistAlbums"));
    m_albums->setViewMode(QListView::IconMode);
    m_albums->setFlow(QListView::LeftToRight);
    m_albums->setWrapping(false);
    m_albums->setMovement(QListView::Static);
    m_albums->setResizeMode(QListView::Adjust);
    m_albums->setGridSize(QSize(124, 156));
    m_albums->setUniformItemSizes(true);
    m_albums->setFixedHeight(166);
    m_albums->setMouseTracking(true);
    m_albums->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_albums->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_albums->setItemDelegate(new AlbumCardDelegate(m_library->artwork(), m_albums));
    right->addWidget(m_albums);
    m_table = new TrackTable(m_details);
    right->addWidget(m_table, 1);
    m_empty = new QLabel(QStringLiteral("还没有艺术家\n添加音乐文件夹后，会按歌曲标签归类展示。"), this);
    m_empty->setObjectName(QStringLiteral("artistsEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(echo::palette().secondary.name()));
    layout->addWidget(m_artists);
    layout->addWidget(m_details, 1);
    layout->addWidget(m_empty, 1);
    connect(m_artists, &QListWidget::currentRowChanged, this, &ArtistsPage::showArtistRow);
    connect(m_albums, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit albumPlayRequested(item->data(Qt::UserRole).toString(), false);
    });
    connect(m_albums, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit albumPlayRequested(item->data(Qt::UserRole).toString(), false);
    });
    for (auto* button : {m_play, m_shuffle})
        connect(button, &QPushButton::clicked, this, [this, button] {
            if (auto* selected = m_artists->currentItem())
                emit artistPlayRequested(selected->data(Qt::UserRole).toString(), button == m_shuffle);
        });
}

void ArtistsPage::showArtistRow(int row)
{
    if (row < 0 || row >= m_artists->count())
        return;
    const auto* selected = m_artists->item(row);
    const auto key = selected->data(Qt::UserRole).toString();
    static_cast<PathLabel*>(m_name)->setPath(selected->text());
    const bool sameArtist = m_selectedArtist == key;
    const int albumsScroll = sameArtist ? m_albums->horizontalScrollBar()->value() : 0;
    m_selectedArtist = key;
    syncAlbumItems(m_albums, m_library->albums(key), QSize(120, 152));
    m_albums->horizontalScrollBar()->setValue(albumsScroll);
    const auto tracks = m_library->artistTracks(key);
    int playable = 0;
    for (const auto& track : tracks)
        playable += !track.missing;
    m_summary->setText(QStringLiteral("%1 张专辑 · %2 首歌曲 · %3 首可播放").arg(m_albums->count()).arg(tracks.size()).arg(playable));
    m_play->setEnabled(playable > 0);
    m_shuffle->setEnabled(playable > 0);
    if (sameArtist)
        m_table->setTracksPreservingView(tracks);
    else
        m_table->setTracks(tracks);
}

void ArtistsPage::reload()
{
    const auto selected = m_artists->currentItem() ? m_artists->currentItem()->data(Qt::UserRole).toString() : QString();
    const int scroll = m_artists->verticalScrollBar()->value();
    {
        const QSignalBlocker blocker(m_artists);
        m_artists->clear();
        int selectedRow = 0;
        for (const auto& artist : m_library->artists()) {
            auto* item = new QListWidgetItem(artist.name);
            item->setData(Qt::UserRole, artist.key);
            item->setData(Qt::UserRole + 1, artist.albums);
            item->setData(Qt::UserRole + 2, artist.tracks);
            item->setSizeHint(QSize(0, 56));
            item->setToolTip(artist.name);
            m_artists->addItem(item);
            if (artist.key == selected)
                selectedRow = m_artists->count() - 1;
        }
        if (m_artists->count())
            m_artists->setCurrentRow(selectedRow);
        m_artists->verticalScrollBar()->setValue(scroll);
    }
    const bool empty = m_artists->count() == 0;
    m_artists->setVisible(!empty);
    m_details->setVisible(!empty);
    m_empty->setVisible(empty);
    if (empty) {
        m_selectedArtist.clear();
        m_table->setTracks({});
        m_albums->clear();
    } else {
        showArtistRow(m_artists->currentRow());
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
    listLayout->setContentsMargins(ContentLeftMargin, 4, ContentRightMargin, 8);
    auto* add = new QPushButton(QStringLiteral("添加文件夹…"), listing);
    add->setObjectName(QStringLiteral("addMusicFolder"));
    add->setFixedHeight(32);
    add->setStyleSheet(actionStyle(false));
    add->setIcon(icon(Icon::Plus, echo::palette().text, 14));
    listLayout->setSpacing(16);
    listLayout->addWidget(add, 0, Qt::AlignRight);
    m_empty = new QLabel(QStringLiteral("还没有文件夹\n手动添加音乐文件夹，只扫描当前层。"), listing);
    m_empty->setObjectName(QStringLiteral("foldersEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    listLayout->addWidget(m_empty, 1);
    m_tree = new FolderTree(listing);
    m_tree->setMouseTracking(true);
    m_tree->setItemDelegate(new FolderRowDelegate(m_tree));
    m_tree->setObjectName(QStringLiteral("folderTree"));
    m_tree->setHeaderLabels(
        { QStringLiteral("名称"), QStringLiteral("路径"), QStringLiteral("歌曲"), QStringLiteral("状态") });
    m_tree->setRootIsDecorated(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 180);
    m_tree->setColumnWidth(2, 120);
    m_tree->setColumnWidth(3, 130);
    m_tree->header()->setFixedHeight(28);
    m_tree->header()->setStyleSheet(QStringLiteral(
        "QHeaderView::section { background: %1; color: %2; border: none; border-bottom: 1px solid %3; "
        "padding: 0 8px; font-size: 11px; font-weight: 500; }")
        .arg(echo::palette().window.name(), echo::palette().secondary.name(), echo::palette().hairline.name(QColor::HexArgb)));
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: transparent; border: none; outline: none; }"
        "QTreeWidget::item { padding: 0 8px; background: transparent; }"
        "QTreeWidget::item:selected, QTreeWidget::item:hover { background: transparent; color: %1; }")
        .arg(echo::palette().text.name()));
    auto treePalette = m_tree->palette();
    treePalette.setColor(QPalette::Highlight, Qt::transparent);
    m_tree->setPalette(treePalette);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    listLayout->addWidget(m_tree, 1);
    m_stack->addWidget(listing);
    auto* detail = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    auto* back = new QPushButton(QStringLiteral("‹ 文件夹"), detail);
    back->setObjectName(QStringLiteral("backToFolders"));
    auto* backRow = new QHBoxLayout;
    backRow->setContentsMargins(ContentLeftMargin, 0, ContentRightMargin, 0);
    back->setFlat(true);
    back->setFixedHeight(26);
    back->setCursor(Qt::PointingHandCursor);
    back->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; color: %1; padding: 0; font-size: 12px; }"
        "QPushButton:hover { color: %2; }").arg(echo::palette().secondary.name(), echo::palette().text.name()));
    backRow->addWidget(back, 0, Qt::AlignLeft);
    backRow->addStretch();
    detailLayout->setSpacing(2);
    detailLayout->addLayout(backRow);
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
    connect(m_library, &Library::scanStarted, this, [this](qint64) { if (isVisible()) reload(); else setProperty("echoDirty", true); });
    connect(m_library, &Library::scanFinished, this, [this] { if (isVisible()) reload(); else setProperty("echoDirty", true); });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = m_tree->itemAt(point);
        if (!item)
            return;
        const auto id = item->data(0, Qt::UserRole).toLongLong();
        const auto source = m_library->root(id);
        QMenu menu(this);
        styleMenu(&menu);
        menu.addAction(QStringLiteral("创建播放列表…"), this, [this, id, source] {
            const auto name = askName(this, QStringLiteral("创建播放列表"), source.name, QStringLiteral("创建"));
            if (name.isEmpty())
                return;
            QVector<qint64> ids;
            if (m_detail->sourceId() == id)
                ids = m_detail->table()->trackModel()->ids();
            else
                for (const auto& track : m_library->folderTracks(id))
                    ids << track.id;
            if (ids.isEmpty()) {
                MessageDialog::information(
                    this, QStringLiteral("无法创建"), QStringLiteral("当前文件夹还没有已知歌曲。"));
                return;
            }
            const auto playlist = m_library->createPlaylistFromFolder(id, name, ids);
            if (playlist)
                emit playlistCreated(playlist);
            else
                MessageDialog::warning(this, QStringLiteral("创建失败"), QStringLiteral("无法保存播放列表。"));
        });
        menu.addAction(QStringLiteral("重新扫描"), this, [this, id] { m_library->refreshRoot(id); });
        menu.addAction(QStringLiteral("在 Finder 中显示"), this, [source] { showInFileManager(source.path); });
        menu.addAction(QStringLiteral("重新定位…"), this, [this, id] {
            const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("重新定位文件夹"));
            if (path.isEmpty())
                return;
            QString error;
            if (!m_library->relocateRoot(id, path, &error))
                MessageDialog::warning(this, QStringLiteral("定位失败"), error);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("移除文件夹…"), this, [this, id, source] {
            if (MessageDialog::question(this, QStringLiteral("移除文件夹"),
                    QStringLiteral("“%1”中的歌曲将退出全局资料库。已有歌单、收藏和播放队列会保留引用，原位置的音乐文件也会保留。")
                        .arg(source.name),
                    QStringLiteral("移除文件夹"), true)
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
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);
    connect(m_searchTimer, &QTimer::timeout, this, &SearchPage::launchSearch);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ContentLeftMargin, 28, ContentRightMargin, 8);
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
    m_artistSection = resultSection(QStringLiteral("艺术家"), m_artists, m_body);
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
    m_query = text.trimmed();
    ++m_searchRevision;
    m_searchTimer->stop();
    if (m_query.isEmpty()) { applySearch({}, {}); return; }
    m_searchTimer->start();
}

void SearchPage::launchSearch()
{
    if (m_searchRunning || m_query.isEmpty()) return;
    m_searchRunning = true;
    const auto revision = m_searchRevision;
    const QString text = m_query, path = m_library->databasePath();
    auto* watcher = new QFutureWatcher<SearchResults>(this);
    connect(watcher, &QFutureWatcher<SearchResults>::finished, this, [this, watcher, revision, text] {
        m_searchRunning = false;
        if (revision == m_searchRevision) applySearch(watcher->result(), text);
        else if (!m_query.isEmpty() && !m_searchTimer->isActive()) m_searchTimer->start(0);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([path, text] { return Library::searchDatabase(path, text); }));
}

void SearchPage::applySearch(const SearchResults& result, const QString& text)
{
    const auto& tracks = result.tracks;
    m_tracks->setTracksPreservingView(tracks);
    m_trackSection->setVisible(!tracks.isEmpty());
    if (!tracks.isEmpty()) {
        const int header = std::max(28, m_tracks->horizontalHeader()->sizeHint().height());
        m_tracks->setFixedHeight(header + static_cast<int>(tracks.size()) * 40 + 2);
    }
    m_albums->clear();
    for (const Album& album : result.albums) {
        auto* item = new QListWidgetItem(album.title + QStringLiteral(" — ") + album.artist);
        item->setData(Qt::UserRole, album.key);
        item->setSizeHint(QSize(0, 34));
        m_albums->addItem(item);
    }
    m_artists->clear();
    for (const Artist& artist : result.artists) {
        auto* item = new QListWidgetItem(artist.name);
        item->setData(Qt::UserRole, artist.key);
        item->setSizeHint(QSize(0, 34));
        m_artists->addItem(item);
    }
    m_playlists->clear();
    for (const Playlist& playlist : result.playlists) {
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
