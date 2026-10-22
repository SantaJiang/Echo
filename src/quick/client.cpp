#include "quick/client.h"
#include "library/library.h"
#include "platform/platform.h"
#include "quick/quickview.h"
#include "quick/uimenu.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include <QGuiApplication>
#include <QCursor>
#include <QFile>
#include <QImageReader>
#include <QFileInfo>
#include <QQuickItem>
#include <QScopeGuard>
#include <QShortcut>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

namespace echo {
namespace {
    QString artUrl(const QString& base, const QString& hash)
    {
        const QString path = base + hash.left(2) + u'/' + hash;
        return hash.isEmpty() || !QFileInfo::exists(path) ? QString() : QUrl::fromLocalFile(path).toString();
    }
    QVariantMap trackRow(const Track& track, const QString& base)
    {
        return { { "key",
                     track.listEntryId ? "entry:" + QString::number(track.listEntryId) : QString::number(track.id) },
            { "id", track.id }, { "title", track.displayTitle() }, { "artist", track.displayArtist() },
            { "album", track.displayAlbum() }, { "duration", formatDuration(track.durationMs) },
            { "missing", track.missing }, { "artwork", artUrl(base, track.artworkHash) },
            { "position", track.playlistPosition } };
    }
    QVector<QVariantMap> albumRows(const QVector<Album>& albums, const QString& base)
    {
        QVector<QVariantMap> rows;
        rows.reserve(albums.size());
        for (const auto& a : albums)
            rows << QVariantMap { { "key", a.key }, { "kind", "album" }, { "title", a.title }, { "subtitle", a.artist },
                { "artwork", artUrl(base, a.artworkHash) }, { "count", a.tracks } };
        return rows;
    }
    QuickPage readPage(const QString& path, const QString& kind, const QString& key, const QString& search, int sort,
        bool descending, const QString& artBase)
    {
        QuickPage result;
        auto finishRows = [&] {
            result.trackRows.reserve(result.tracks.size());
            for (const auto& track : result.tracks) {
                result.trackRows << trackRow(track, artBase);
                result.duration += track.durationMs;
            }
        };
        if (kind == "search") {
            const auto found = Library::searchDatabase(path, search);
            result.tracks = found.tracks;
            result.cards = albumRows(found.albums, artBase);
            for (const auto& a : found.artists)
                result.cards << QVariantMap { { "key", a.key }, { "kind", "artist" }, { "title", a.name },
                    { "subtitle", QStringLiteral("%1 张专辑 · %2 首歌曲").arg(a.albums).arg(a.tracks) },
                    { "artwork", QString() } };
            for (const auto& p : found.playlists)
                result.cards << QVariantMap { { "key", QString::number(p.id) }, { "kind", "playlist" },
                    { "title", p.name }, { "subtitle", QStringLiteral("播放列表") }, { "artwork", QString() } };
            finishRows();
            return result;
        }
        const QString connection = "quick-" + QUuid::createUuid().toString(QUuid::Id128);
        const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
        {
            Database db;
            if (!db.open(path, connection, &result.error, true))
                return result;
            QSqlQuery transaction(QSqlDatabase::database(connection));
            transaction.exec("BEGIN");
            const QString scope = "WHERE " + Database::libraryScope();
            const QStringList columns { "track_number", "title", "artist", "album", "duration_ms" };
            const QString order = QStringLiteral("ORDER BY %1 %2,disc_number,track_number,title,id")
                                      .arg(columns.value(sort, "title"), descending ? "DESC" : "ASC");
            if (kind == "songs")
                result.tracks = db.queryTracks(scope, {}, order);
            else if (kind == "album")
                result.tracks = db.queryTracks(
                    scope + " AND album_key=?", { key }, sort ? order : "ORDER BY disc_number,track_number,title,id");
            else if (kind == "artist") {
                result.cards = albumRows(db.albums(key), artBase);
                result.tracks = db.queryTracks(
                    scope + " AND artist_key=?", { key }, sort ? order : "ORDER BY album,disc_number,track_number,id");
            } else if (kind == "source")
                result.tracks = db.queryTracks("WHERE root_id=?", { key.toLongLong() },
                    sort ? order : "ORDER BY album,disc_number,track_number,title,id");
            else if (kind == "playlist") {
                // Keep entry identities, duplicates and persisted ordering intact.
                QSqlQuery entries(QSqlDatabase::database(connection));
                entries.prepare("SELECT id,track_id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id");
                entries.addBindValue(key.toLongLong());
                entries.exec();
                QVector<qint64> ids, entryIds;
                while (entries.next()) {
                    entryIds << entries.value(0).toLongLong();
                    ids << entries.value(1).toLongLong();
                }
                result.tracks = db.tracksByIds(ids);
                for (int i = 0; i < result.tracks.size(); ++i) {
                    result.tracks[i].listEntryId = entryIds[i];
                    result.tracks[i].playlistPosition = i;
                }
                if (sort)
                    std::stable_sort(result.tracks.begin(), result.tracks.end(), [=](const Track& a, const Track& b) {
                        int c = sort == 4 ? (a.durationMs > b.durationMs          ? 1
                                                    : a.durationMs < b.durationMs ? -1
                                                                                  : 0)
                                          : QString::localeAwareCompare(sort == 2 ? a.displayArtist()
                                                    : sort == 3                   ? a.displayAlbum()
                                                                                  : a.displayTitle(),
                                                sort == 2       ? b.displayArtist()
                                                    : sort == 3 ? b.displayAlbum()
                                                                : b.displayTitle());
                        return descending ? c > 0 : c < 0;
                    });
            } else if (kind == "albums")
                result.cards = albumRows(db.albums({}), artBase);
            else if (kind == "artists") {
                for (const auto& a : db.artists())
                    result.cards << QVariantMap { { "key", a.key }, { "kind", "artist" }, { "title", a.name },
                        { "subtitle", QStringLiteral("%1 张专辑 · %2 首歌曲").arg(a.albums).arg(a.tracks) },
                        { "artwork", QString() } };
            } else if (kind == "sources") {
                for (const auto& r : db.roots())
                    result.cards << QVariantMap { { "key", QString::number(r.id) }, { "kind", "source" },
                        { "title", r.name },
                        { "subtitle",
                            QStringLiteral("%1 首歌曲 · %2")
                                .arg(r.tracks)
                                .arg(r.status == "available" ? QStringLiteral("可用") : QStringLiteral("暂时不可用")) },
                        { "path", r.path }, { "artwork", QString() }, { "missing", r.status != "available" } };
            }
            transaction.exec("COMMIT");
        }
        finishRows();
        return result;
    }
}

Client::Client(Library* library, Player* player, AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_library(library)
    , m_player(player)
    , m_engine(engine)
    , m_menuBar(std::make_unique<UiMenu>())
{
    m_pool.setMaxThreadCount(1);
    m_reduceMotion = library->setting("quick_reduce_motion", "0") == "1";
    const auto widths = library->setting("track_columns").split(u',');
    if (widths.size() == 5)
        for (int i = 0; i < 5; ++i)
            m_columns[i] = std::clamp(widths[i].toInt(), i == 0 ? 40 : 64, 1200);
    m_columnTimer.setSingleShot(true);
    m_columnTimer.setInterval(200);
    connect(&m_columnTimer, &QTimer::timeout, this, [this] {
        QStringList widths;
        for (const auto& v : m_columns)
            widths << QString::number(v.toInt());
        m_library->setSetting("track_columns", widths.join(u','));
    });
    m_navigationTimer.setSingleShot(true);
    m_navigationTimer.setInterval(120);
    connect(&m_navigationTimer, &QTimer::timeout, this, &Client::requestPage);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(180);
    connect(&m_searchTimer, &QTimer::timeout, this, &Client::requestPage);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, &Client::refresh);
    auto changed = [this] {
        ++m_generation;
        m_refreshTimer.start();
    };
    connect(library, &Library::changed, this, changed);
    connect(library, &Library::sourceChanged, this, [changed](qint64) { changed(); });
    connect(library, &Library::favoritesChanged, this, [this] {
        syncTrackState();
        if (m_route.kind == "playlist" && m_route.key.toLongLong() == m_library->favoritesPlaylistId()) {
            ++m_generation;
            m_refreshTimer.start();
        }
    });
    connect(library, &Library::favoritesFailed, this, &Client::report);
    connect(library, &Library::scanProgress, this, [this](int done, int total) {
        if (total > done)
            report(QStringLiteral("正在扫描 %1 / %2").arg(done).arg(total));
    });
    connect(library, &Library::scanFinished, this, [this] { report({}); });
    connect(library, &Library::scanFailed, this, &Client::report);
    connect(library, &Library::scanWarning, this, &Client::report);
    connect(player, &Player::playbackMessage, this, &Client::report);
    connect(player, &Player::trackUnavailable, this, &Client::showUnavailable);
    connect(player, &Player::trashFinished, this, [this](const QString& text) {
        requestDialog({{"kind", "message"}, {"title", QStringLiteral("移到废纸篓")},
            {"text", text.section(u'\n', 0, 0)}, {"details", text.section(u'\n', 1)}});
    });
    connect(engine, &AudioEngine::errorOccurred, this, &Client::report);
    connect(engine, &AudioEngine::exclusiveFallback, this,
        [this](const QString& reason) { report(QStringLiteral("已回退共享输出：") + reason); });
    connect(player, &Player::changed, this, [this] {
        syncTrackState();
        syncQueue();
    });
    connect(&m_query, &QFutureWatcher<QuickPage>::finished, this, [this] {
        if (m_runningGeneration != m_generation) {
            requestPage();
            return;
        }
        const auto result = m_query.result();
        m_busy = false;
        if (!result.error.isEmpty()) {
            report(result.error);
            emit pageChanged();
            return;
        }
        m_pageTracks = result.tracks;
        m_totalDuration = result.duration;
        m_tracks.replace(result.trackRows);
        m_tracks.setTrackState(m_library->favoriteIds(), m_player->currentTrack().id);
        m_cards.replace(result.cards);
        QSet<QString> retained;
        for (int i = 0; !m_selection.isEmpty() && i < m_tracks.count(); ++i)
            if (m_selection.contains(rowKey(i)))
                retained.insert(rowKey(i));
        m_selection = retained;
        ++m_selectionRevision;
        emit selectionChanged();
        emit pageChanged();
        if (!m_pendingPlay.isEmpty() && m_pendingPlay == routeKey()) {
            m_pendingPlay.clear();
            playAll(m_pendingShuffle);
        }
    });
    connect(player, &Player::changed, this, &Client::preferencesChanged);
    connect(player, &Player::normalizationChanged, this, &Client::preferencesChanged);
    connect(player, &Player::spatialChanged, this, &Client::preferencesChanged);
    connect(this, &Client::reduceMotionChanged, this, &Client::preferencesChanged);
    setupMenus();
    rebuildNavigation();
    requestPage();
}
Client::~Client()
{
    if (m_columnTimer.isActive()) {
        m_columnTimer.stop();
        QStringList widths;
        for (const auto& v : m_columns)
            widths << QString::number(v.toInt());
        m_library->setSetting("track_columns", widths.join(u','));
    }
    m_query.waitForFinished();
    m_pool.waitForDone();
}
void Client::attach(QuickView* window)
{
    m_window = window;
    window->setProperty("echoMainWindow", true);
    qGuiApp->setQuitOnLastWindowClosed(false);
    installTrayIcon(this, [this] { showMainWindow(); }, [this] { showSettings(); },
        [this](const QPoint& position) { emit trayMenuRequested(position); });
    installApplicationMenu(this, menuData(*m_menuBar, true), [this](int token) { triggerMenu(token); });
    installMediaIntegration(window, [this](MediaCommand command, qint64 value) {
        switch (command) {
        case MediaCommand::Toggle:
            m_player->toggle();
            break;
        case MediaCommand::Next:
            m_player->next();
            break;
        case MediaCommand::Previous:
            m_player->previous();
            break;
        case MediaCommand::Seek:
            m_player->seek(value);
            break;
        }
    });
    auto shortcut = [this, window](QKeySequence key, std::function<void()> call) {
        auto* s = new QShortcut(key, window);
        connect(s, &QShortcut::activated, this, std::move(call));
    };
    shortcut(QKeySequence::Quit, [] { quitApplication(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_O), [this] { addMusicFolder(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { showSettings(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { createPlaylist(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { emit focusSearch(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Right), [this] { m_player->next(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Left), [this] { m_player->previous(); });
    shortcut(
        QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { m_player->setVolume(std::min(1.f, m_player->volume() + .05f)); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Down),
        [this] { m_player->setVolume(std::max(0.f, m_player->volume() - .05f)); });
    auto* clock = new QTimer(this);
    clock->setInterval(250);
    connect(clock, &QTimer::timeout, this, [this] {
        syncSystem();
    });
    clock->start();
    connect(m_library->artwork(), &ArtworkCache::ready, this, [this](const QString& hash, int size) {
        const auto track = m_player->currentTrack();
        if (hash == track.artworkHash && size == 160) {
            m_systemArtwork = m_library->artwork()->pixmap(hash, size).toImage();
            syncSystem(true);
        }
    });
    connect(m_player, &Player::changed, this, [this] { syncSystem(); });
    connect(m_engine, &AudioEngine::positionChanged, this, [this] { syncSystem(true); });
    connect(window, &QuickView::hiddenToTray, m_player, &Player::saveSession);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_window && !m_window->isVisible() && (!m_mini || !m_mini->isVisible())
            && m_dialog.isEmpty())
            showMainWindow();
    });
    syncSystem(true);
}
void Client::syncSystem(bool force)
{
    const auto track = m_player->currentTrack();
    if (m_systemArtworkHash != track.artworkHash) {
        m_systemArtworkHash = track.artworkHash;
        m_systemArtwork = m_library->artwork()->pixmap(track.artworkHash, 160).toImage();
    }
    updateSystemNowPlaying(track, m_systemArtwork, m_player->positionMs(), track.durationMs,
        m_player->state() == AudioEngine::State::Playing, force);
}
QVariantMap Client::page() const
{
    QString subtitle = QStringLiteral("%1 首歌曲 · %2").arg(m_pageTracks.size()).arg(formatDuration(m_totalDuration));
    if (m_route.kind == "albums" || m_route.kind == "artists" || m_route.kind == "sources")
        subtitle = QStringLiteral("%1 个项目").arg(m_cards.count());
    if (m_route.kind == "source")
        subtitle = m_sourcePath + " · " + subtitle;
    return { { "kind", m_route.kind }, { "key", m_route.key }, { "title", m_route.title }, { "subtitle", subtitle },
        { "busy", m_busy }, { "canBack", !m_history.isEmpty() }, { "sort", m_route.sort },
        { "descending", m_route.descending }, { "query", m_search }, { "route", routeKey() },
        { "artwork", m_tracks.count() ? m_tracks.get(0).value("artwork") : QVariant(QString()) } };
}
QString Client::routeKey() const { return m_route.kind + ":" + m_route.key; }
void Client::navigate(const QString& kind, const QString& key, const QString& title)
{
    static const QSet<QString> allowed { "songs", "albums", "artists", "sources", "album", "artist", "source",
        "playlist" };
    if (!allowed.contains(kind))
        return;
    if (kind == m_route.kind && key == m_route.key)
        return;
    m_selections[routeKey()] = m_selection;
    m_history << m_route;
    m_route = { kind, key, title.isEmpty() ? kind : title };
    m_search.clear();
    m_searchTimer.stop();
    m_pendingPlay.clear();
    m_sourcePath = kind == "source" ? m_library->root(key.toLongLong()).path : QString();
    m_selection = m_selections.value(routeKey());
    m_anchor = -1;
    ++m_selectionRevision;
    emit selectionChanged();
    ++m_generation;
    m_busy = true;
    m_navigationTimer.start();
    emit navigationStarted();
}
void Client::back()
{
    if (m_route.kind == "search") {
        search({});
        return;
    }
    if (m_history.isEmpty())
        return;
    m_selections[routeKey()] = m_selection;
    const Route previous = m_history.takeLast();
    m_route = previous;
    m_selection = m_selections.value(routeKey());
    m_search = m_route.kind == "search" ? m_route.key : QString();
    m_sourcePath = m_route.kind == "source" ? m_library->root(m_route.key.toLongLong()).path : QString();
    m_searchTimer.stop();
    m_pendingPlay.clear();
    ++m_selectionRevision;
    emit selectionChanged();
    ++m_generation;
    m_busy = true;
    m_navigationTimer.start();
    emit navigationStarted();
}
void Client::search(const QString& text)
{
    const QString query = text.trimmed();
    if (m_search == query)
        return;
    if (m_route.kind != "search")
        m_beforeSearch = m_route;
    m_search = query;
    m_searchTimer.stop();
    m_navigationTimer.stop();
    m_busy = true;
    emit navigationStarted();
    ++m_generation;
    m_pendingPlay.clear();
    clearSelection();
    if (query.isEmpty()) {
        m_route = m_beforeSearch;
        m_navigationTimer.start();
        return;
    }
    m_route = { "search", query, QStringLiteral("搜索结果") };
    m_busy = true;
    m_searchTimer.start();
}
void Client::refresh()
{
    if (m_route.kind == "source")
        m_sourcePath = m_library->root(m_route.key.toLongLong()).path;
    rebuildNavigation();
    syncQueue(true);
    requestPage();
}
void Client::requestPage()
{
    // One worker, at most one pending refresh; obsolete snapshots never reach QML.
    if (m_searchTimer.isActive() || m_navigationTimer.isActive())
        return;
    if (m_query.isRunning()) {
        ++m_generation;
        m_dirty = true;
        return;
    }
    m_dirty = false;
    m_busy = true;
    m_runningGeneration = ++m_generation;
    emit pageChanged();
    const auto route = m_route;
    const auto query = m_search;
    const auto path = m_library->databasePath();
    const auto base = m_library->artwork()->filePath({});
    m_query.setFuture(QtConcurrent::run(&m_pool, [path, route, query, base] {
        return readPage(path, route.kind, route.key, query, route.sort, route.descending, base);
    }));
}
void Client::rebuildNavigation()
{
    QVector<QVariantMap> rows;
    const QStringList kinds { "songs", "albums", "artists", "sources" },
        titles { QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺术家"), QStringLiteral("文件夹") };
    for (int i = 0; i < kinds.size(); ++i)
        rows << QVariantMap { { "key", kinds[i] }, { "kind", kinds[i] }, { "title", titles[i] },
            { "icon", i == 3 ? "folders" : kinds[i] }, { "depth", 0 } };
    const auto lists = m_library->playlists();
    const auto folders = m_library->playlistFolders();
    for (const auto& p : lists)
        if (p.isFavorites())
            rows << QVariantMap { { "key", QString::number(p.id) }, { "kind", "playlist" }, { "title", p.name },
                { "icon", "heartFilled" }, { "depth", 0 }, { "protected", true } };
    rows << QVariantMap { { "key", "playlistSection" }, { "kind", "playlistSection" },
        { "title", QStringLiteral("播放列表") }, { "icon", "" }, { "depth", 0 } };
    QSet<qint64> visited;
    std::function<void(qint64, int)> append = [&](qint64 parent, int depth) {
        for (const auto& f : folders)
            if (f.parentId == parent && !visited.contains(f.id)) {
                visited.insert(f.id);
                rows << QVariantMap { { "key", QString::number(f.id) }, { "kind", "listFolder" }, { "title", f.name },
                    { "icon", "folders" }, { "depth", depth } };
                append(f.id, depth + 1);
            }
        for (const auto& p : lists)
            if (!p.isFavorites() && p.folderId == parent)
                rows << QVariantMap { { "key", QString::number(p.id) }, { "kind", "playlist" }, { "title", p.name },
                    { "icon", "playlist" }, { "depth", depth } };
    };
    append(0, 0);
    m_navigation.replace(rows);
    if (m_route.kind == "playlist")
        for (const auto& p : lists)
            if (QString::number(p.id) == m_route.key) {
                m_route.title = p.name;
                break;
            }
}
void Client::syncTrackState()
{
    const auto id = m_player->currentTrack().id;
    const bool playing = m_player->state() == AudioEngine::State::Playing;
    if (m_lastPlayingId == id && m_lastPlaying == playing && m_lastFavorites == m_library->favoriteIds())
        return;
    m_tracks.setTrackState(m_library->favoriteIds(), id);
    m_lastPlayingId = id;
    m_lastPlaying = playing;
    m_lastFavorites = m_library->favoriteIds();
}
void Client::syncQueue(bool force)
{
    const auto entries = m_player->queue();
    const int index = m_player->index();
    if (!force && m_lastQueue == entries && m_lastQueueIndex == index)
        return;
    m_lastQueue = entries;
    m_lastQueueIndex = index;
    QVector<qint64> ids;
    for (const auto& e : entries)
        ids << e.trackId;
    const auto tracks = m_library->tracks(ids);
    QVector<QVariantMap> rows;
    const auto base = m_library->artwork()->filePath({});
    for (int i = 0; i < entries.size(); ++i) {
        auto row = trackRow(tracks.value(i), base);
        row["key"] = QString::number(entries[i].token);
        row["current"] = i == index;
        rows << row;
    }
    m_queue.replace(rows);
}
void Client::openCard(int row)
{
    const auto card = m_cards.get(row);
    if (!card.isEmpty())
        navigate(card.value("kind").toString(), card.value("key").toString(), card.value("title").toString());
}
void Client::playCard(int row, bool shuffle)
{
    openCard(row);
    m_pendingPlay = routeKey();
    m_pendingShuffle = shuffle;
}
void Client::playRow(int row)
{
    if (m_busy || row < 0 || row >= m_pageTracks.size())
        return;
    const auto track = m_pageTracks[row];
    if (track.missing || !QFileInfo(track.path).isFile()) {
        showUnavailable(track.id);
        return;
    }
    if ((m_route.kind == "source" || m_route.kind == "playlist") && !m_player->replaceContextQueue())
        m_player->playSinglePreservingQueue(track.id);
    else {
        QVector<qint64> ids;
        for (const auto& t : m_pageTracks)
            ids << t.id;
        m_player->playTracks(ids, row, m_route.kind == "album");
    }
}
void Client::playAll(bool shuffle)
{
    if (m_busy || m_pageTracks.isEmpty())
        return;
    QVector<qint64> ids;
    for (const auto& t : m_pageTracks)
        ids << t.id;
    m_player->playTracks(ids, 0, m_route.kind == "album", shuffle);
}
void Client::sortBy(int column)
{
    if (column < 0 || column > 4)
        return;
    if (m_route.sort == column)
        m_route.descending = !m_route.descending;
    else {
        m_route.sort = column;
        m_route.descending = false;
    }
    ++m_generation;
    requestPage();
}
QString Client::rowKey(int row) const { return m_tracks.keyAt(row); }
bool Client::isSelected(int row) const { return m_selection.contains(rowKey(row)); }
void Client::selectRow(int row, int modifiers)
{
    if (row < 0 || row >= m_tracks.count())
        return;
    if (modifiers & Qt::ShiftModifier) {
        if (m_anchor < 0)
            m_anchor = row;
        if (!(modifiers & (Qt::ControlModifier | Qt::MetaModifier)))
            m_selection.clear();
        for (int i = std::min(row, m_anchor); i <= std::max(row, m_anchor); ++i)
            m_selection.insert(rowKey(i));
    } else if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
        const auto key = rowKey(row);
        if (m_selection.contains(key))
            m_selection.remove(key);
        else
            m_selection.insert(key);
        m_anchor = row;
    } else {
        m_selection = { rowKey(row) };
        m_anchor = row;
    }
    ++m_selectionRevision;
    emit selectionChanged();
}
void Client::selectAll()
{
    for (int i = 0; i < m_tracks.count(); ++i)
        m_selection.insert(rowKey(i));
    ++m_selectionRevision;
    emit selectionChanged();
}
void Client::clearSelection()
{
    m_selection.clear();
    m_anchor = -1;
    ++m_selectionRevision;
    emit selectionChanged();
}
QVector<qint64> Client::selectedIds() const
{
    QVector<qint64> ids;
    for (int i = 0; i < m_pageTracks.size(); ++i)
        if (isSelected(i))
            ids << m_pageTracks[i].id;
    return ids;
}
void Client::favoriteRow(int row)
{
    if (row >= 0 && row < m_pageTracks.size()) {
        const auto id = m_pageTracks[row].id;
        m_library->setFavorites({ id }, !m_library->isFavorite(id));
    }
}
void Client::removeSelected()
{
    if (m_route.kind != "playlist")
        return;
    if (m_route.key.toLongLong() == m_library->favoritesPlaylistId())
        m_library->setFavorites(selectedIds(), false);
    else {
        QVector<int> positions;
        for (int i = 0; i < m_pageTracks.size(); ++i)
            if (isSelected(i))
                positions << m_pageTracks[i].playlistPosition;
        m_library->removePlaylistPositions(m_route.key.toLongLong(), positions);
    }
}
void Client::moveTrack(int from, int to)
{
    if (m_route.kind != "playlist" || m_route.sort || m_route.key.toLongLong() == m_library->favoritesPlaylistId()
        || from < 0 || to < 0 || from >= m_pageTracks.size() || to >= m_pageTracks.size())
        return;
    m_library->movePlaylistItem(
        m_route.key.toLongLong(), m_pageTracks[from].playlistPosition, m_pageTracks[to].playlistPosition);
}
void Client::queueAction(const QString& a, int row, int to)
{
    if (a == "clear")
        m_player->clearUpcoming();
    else if (row >= 0 && row < m_queue.count()) {
        if (a == "play")
            m_player->activateQueueRow(row);
        else if (a == "remove")
            m_player->removeQueueRow(row);
        else if (a == "move" && to >= 0 && to < m_queue.count())
            m_player->moveQueue(row, to);
    }
}
void Client::setScroll(double offset) { m_scrolls[routeKey()] = offset; }
double Client::scroll() const { return m_scrolls.value(routeKey()); }
void Client::resizeColumn(int column, double width)
{
    if (column < 0 || column >= 5)
        return;
    m_columns[column] = std::clamp(width, column == 0 ? 40. : 64., 1200.);
    emit columnsChanged();
    m_columnTimer.start();
}
void Client::setReduceMotion(bool value)
{
    if (m_reduceMotion == value)
        return;
    m_reduceMotion = value;
    m_library->setSetting("quick_reduce_motion", value ? "1" : "0");
    emit reduceMotionChanged();
}
void Client::report(const QString& text)
{
    if (text == m_status)
        return;
    m_status = text;
    emit statusChanged();
}
QVariantMap Client::preferences() const
{
    return {{"replaceQueue", m_player->replaceContextQueue()}, {"normalize", m_player->normalizationEnabled()},
        {"spatial", m_player->spatialEnabled()}, {"spatialStatus", m_player->spatialStatus()},
        {"exclusive", m_player->exclusive()}, {"replayGain", m_player->replayGainEnabled()}, {"reduceMotion", m_reduceMotion}};
}
void Client::setPreference(const QString& key, bool value)
{
    if (key == "replaceQueue") m_player->setReplaceContextQueue(value);
    else if (key == "normalize") m_player->setNormalizationEnabled(value);
    else if (key == "spatial") m_player->setSpatialEnabled(value);
    else if (key == "exclusive") m_player->setExclusive(value);
    else if (key == "replayGain") m_player->setReplayGainEnabled(value);
    else if (key == "reduceMotion") setReduceMotion(value);
    emit preferencesChanged();
}
void Client::requestDialog(QVariantMap data, Reply reply)
{
    data["token"] = ++m_dialogToken;
    if (!m_dialog.isEmpty()) { m_pendingDialogs.append({std::move(data), std::move(reply)}); return; }
    m_dialog = std::move(data); m_reply = std::move(reply);
    emit dialogChanged();
}
void Client::answerDialog(int token, const QVariantMap& answer)
{
    if (m_dialog.isEmpty() || m_dialog.value("token").toInt() != token) return;
    auto reply = std::move(m_reply);
    m_dialog.clear(); emit dialogChanged();
    if (reply) reply(answer);
    if (m_dialog.isEmpty() && !m_pendingDialogs.isEmpty()) {
        auto next = m_pendingDialogs.takeFirst();
        m_dialog = std::move(next.data); m_reply = std::move(next.reply); emit dialogChanged();
    }
}
void Client::askName(const QString& title, const QString& initial, const QString& accept, std::function<void(QString)> reply)
{
    requestDialog({{"kind", "name"}, {"title", title}, {"initial", initial}, {"accept", accept}},
        [reply = std::move(reply)](const QVariantMap& a) { if (a.value("accepted").toBool()) reply(a.value("name").toString().trimmed()); });
}
void Client::choosePath(const QString& title, const QString& mode, const QStringList& filters, std::function<void(QString)> reply)
{
    requestDialog({{"kind", "file"}, {"title", title}, {"mode", mode}, {"filters", filters}},
        [reply = std::move(reply)](const QVariantMap& a) {
            if (!a.value("accepted").toBool()) return;
            const auto value = a.value("path").toString(); const QUrl url(value);
            const auto path = url.isLocalFile() ? url.toLocalFile() : value;
            if (!path.isEmpty()) reply(path);
        });
}
QVariantList Client::menuData(const UiMenu& menu, bool application)
{
    QVariantList result;
    auto& callbacks = application ? m_appCallbacks : m_menuCallbacks;
    for (const auto& entry : menu.entries) {
        const int token = ++m_menuToken;
        QVariantMap item{{"text", entry.text}, {"token", token}, {"separator", entry.separator}};
        if (entry.invoke) callbacks[token] = entry.invoke;
        if (entry.submenu) item["children"] = menuData(*entry.submenu, application);
        result << item;
    }
    return result;
}
void Client::triggerMenu(int token)
{
    auto invoke = m_menuCallbacks.value(token, m_appCallbacks.value(token));
    if (invoke) invoke();
}
QPointF Client::cursorPosition() const { return m_window ? m_window->mapFromGlobal(QCursor::pos()) : QPoint(); }
void Client::popup(UiMenu& menu) { m_menuCallbacks.clear(); emit menuRequested(menuData(menu)); }
void Client::setMiniWindow(QWindow* window)
{
    m_mini = window;
    if (window) { window->setProperty("echoMiniPlayer", true); applyMiniWindowChrome(window); }
}
void Client::showMainWindow()
{
    if (m_mini) m_mini->hide();
    emit mainRequested();
    if (!m_window) return;
    if (m_window->visibility() == QWindow::Minimized) m_window->showNormal(); else m_window->show();
    m_window->raise(); m_window->requestActivate();
}
void Client::showSettings() { showMainWindow(); emit settingsRequested(); }
void Client::showMini()
{
    emit miniRequested();
    if (m_window) m_window->hide();
    if (m_mini) { m_mini->show(); m_mini->raise(); m_mini->requestActivate(); }
}
void Client::openUrls(const QVariantList& urls)
{
    QStringList paths;
    for (const auto& value : urls) { const QUrl url(value.toString()); if (url.isLocalFile()) paths << url.toLocalFile(); }
    openPaths(paths);
}
void Client::openPaths(const QStringList& paths)
{
    QVector<qint64> ids;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) m_library->addRoot(info.absoluteFilePath());
        else ids += m_library->importFiles({info.absoluteFilePath()});
    }
    if (!ids.isEmpty()) m_player->playTracks(ids, 0, false);
}
void Client::addMusicFolder()
{
    choosePath(QStringLiteral("添加音乐文件夹"), "folder", {}, [this](const QString& path) {
        const auto id = m_library->addRoot(path);
        if (!id) { report(QStringLiteral("无法登记文件夹，请检查目录和权限")); return; }
        navigate("source", QString::number(id), QFileInfo(path).fileName());
    });
}
void Client::createPlaylist(qint64 folder)
{
    askName(QStringLiteral("新建歌单"), QStringLiteral("新歌单"), QStringLiteral("创建"), [this, folder](const QString& name) {
        if (name.isEmpty()) return;
        const auto id = m_library->createPlaylist(name, folder);
        if (id) navigate("playlist", QString::number(id), name);
    });
}
void Client::createFolder()
{
    askName(QStringLiteral("新建歌单文件夹"), QStringLiteral("新文件夹"), QStringLiteral("创建"), [this](const QString& name) {
        if (!name.isEmpty()) m_library->createPlaylistFolder(name);
    });
}
void Client::importPlaylist()
{
    const auto target = m_route.kind == "playlist" ? m_route.key.toLongLong() : 0;
    choosePath(QStringLiteral("导入播放列表"), "open", {"播放列表 (*.m3u *.m3u8)"}, [this, target](const QString& path) {
        auto id = target;
        if (id <= 0 || id == m_library->favoritesPlaylistId()) id = m_library->createPlaylist(QFileInfo(path).completeBaseName());
        QString error;
        if (!m_library->importM3u(id, path, &error)) { report(error); return; }
        navigate("playlist", QString::number(id), QFileInfo(path).completeBaseName()); refresh();
    });
}
void Client::exportPlaylist()
{
    if (m_route.kind != "playlist") return;
    const auto target = m_route.key.toLongLong();
    choosePath(QStringLiteral("导出播放列表"), "save", {"播放列表 (*.m3u8)"}, [this, target](QString path) {
        if (QFileInfo(path).suffix().isEmpty()) path += ".m3u8";
        QString error; if (!m_library->exportM3u(target, path, &error)) report(error);
    });
}
void Client::showInfo(qint64 id)
{
    const auto track = m_library->track(id); if (!track.id) return;
    requestDialog({{"kind", "track"}, {"title", QStringLiteral("歌曲信息")}, {"trackTitle", track.title},
        {"artist", track.artist}, {"album", track.album}, {"albumArtist", track.albumArtist},
        {"trackNumber", track.trackNumber}, {"year", track.year}, {"cue", !track.cuePath.isEmpty()},
        {"artwork", artUrl(m_library->artwork()->filePath({}), track.artworkHash)}, {"path", track.path}},
        [this, id, cue = !track.cuePath.isEmpty()](const QVariantMap& a) {
            if (!a.value("accepted").toBool()) return;
            TagEdit edit; edit.title = a.value("title").toString(); edit.artist = a.value("artist").toString();
            edit.album = a.value("album").toString(); edit.albumArtist = a.value("albumArtist").toString();
            edit.trackNumber = std::clamp(a.value("trackNumber").toInt(), 0, 999);
            edit.year = std::clamp(a.value("year").toInt(), 0, 9999);
            const QUrl art(a.value("artFile").toString());
            if (!cue && art.isLocalFile()) {
                QFile file(art.toLocalFile()); QImageReader reader(art.toLocalFile());
                if (!file.open(QIODevice::ReadOnly) || !reader.canRead()) { report(QStringLiteral("无法读取所选封面")); return; }
                edit.editArtwork = true; edit.artwork = file.readAll();
                edit.artworkMime = reader.format().toLower() == "png" ? "image/png" : "image/jpeg";
            }
            QString error; if (!m_library->saveTags(id, edit, &error)) report(error);
        });
}
void Client::locateTrack(qint64 id)
{
    choosePath(QStringLiteral("重新定位音乐文件"), "open", {}, [this, id](const QString& path) {
        bool registered = false;
        for (const auto& root : m_library->roots()) if (root.path == QFileInfo(path).absolutePath()) registered = true;
        requestDialog({{"kind", "confirm"}, {"title", QStringLiteral("确认歌曲位置")},
            {"text", path + u'\n' + (registered ? QStringLiteral("属于已添加的文件夹，将进入全局资料库。")
                : QStringLiteral("文件夹未添加，只保留在队列、收藏或歌单中。"))}, {"accept", QStringLiteral("确认")}},
            [this, id, path](const QVariantMap& a) { if (a.value("accepted").toBool()) {
                QString error; if (!m_library->relocateTrack(id, path, &error)) report(error);
            }});
    });
}
void Client::showUnavailable(qint64 id)
{
    m_library->recheckTrack(id); const auto track = m_library->track(id);
    requestDialog({{"kind", "unavailable"}, {"title", QStringLiteral("歌曲暂时无法播放")},
        {"text", track.displayTitle() + u'\n' + track.path + QStringLiteral("\n文件可能已移动、磁盘未连接或访问权限已改变。")}},
        [this, id](const QVariantMap& a) {
            if (a.value("choice") == "check") m_library->recheckTrack(id);
            else if (a.value("choice") == "locate") locateTrack(id);
        });
}
void Client::trashTracks(const QVector<qint64>& ids)
{
    QStringList paths, details; int affected = 0;
    for (const auto& t : m_library->tracks(ids)) if (t.id && !t.path.isEmpty() && !paths.contains(t.path)) paths << t.path;
    for (const auto& path : paths) { details << path; for (const auto& t : m_library->tracksAtPath(path)) { details << "    " + t.displayTitle(); ++affected; } }
    if (paths.isEmpty()) return;
    requestDialog({{"kind", "confirm"}, {"title", QStringLiteral("移到废纸篓")}, {"destructive", true},
        {"text", QStringLiteral("将 %1 个音乐文件移到废纸篓？\n会影响 %2 首曲目，包括同一音频文件中的其他 CUE 曲目。引用保留为不可用，封面、歌词和 CUE 文件会保留。").arg(paths.size()).arg(affected)},
        {"details", details.join(u'\n')}, {"accept", QStringLiteral("移到废纸篓")}},
        [this, paths](const QVariantMap& a) { if (a.value("accepted").toBool()) m_player->trashFiles(paths); });
}
void Client::addToPlaylist(const QVector<qint64>& ids)
{
    UiMenu menu;
    for (const auto& p : m_library->playlists()) menu.addAction(p.name, this, [this, p, ids] { m_library->addToPlaylist(p.id, ids); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("新建歌单…"), this, [this, ids] {
        askName(QStringLiteral("新建歌单"), QStringLiteral("新歌单"), QStringLiteral("创建"), [this, ids](const QString& name) {
            if (!name.isEmpty()) m_library->addToPlaylist(m_library->createPlaylist(name), ids);
        });
    }); popup(menu);
}
void Client::trackMenu(int row)
{
    if (row < 0 || row >= m_pageTracks.size()) return;
    if (!isSelected(row)) selectRow(row);
    const auto ids = selectedIds(); const auto track = m_pageTracks[row];
    UiMenu menu;
    menu.addAction(QStringLiteral("播放"), this, [this, row] { playRow(row); });
    menu.addAction(QStringLiteral("下一首播放"), this, [this, ids] { m_player->enqueueNext(ids); });
    menu.addAction(QStringLiteral("加入播放队列"), this, [this, ids] { m_player->enqueue(ids); });
    menu.addAction(QStringLiteral("添加到播放列表…"), this, [this, ids] { addToPlaylist(ids); });
    menu.addAction(m_library->isFavorite(track.id) ? QStringLiteral("取消喜欢") : QStringLiteral("添加到我喜欢的"),
        this, [this, ids, liked = !m_library->isFavorite(track.id)] { m_library->setFavorites(ids, liked); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("歌曲信息…"), this, [this, track] { showInfo(track.id); });
    menu.addAction(QStringLiteral("在文件管理器中显示"), this, [track] { showInFileManager(track.path); });
    if (track.missing) {
        menu.addAction(QStringLiteral("重新检查"), this, [this, track] { m_library->recheckTrack(track.id); });
        menu.addAction(QStringLiteral("重新定位…"), this, [this, track] { locateTrack(track.id); });
    }
    if (m_route.kind == "playlist") {
        menu.addSeparator();
        const auto playlistId = m_route.key.toLongLong();
        menu.addAction(playlistId == m_library->favoritesPlaylistId() ? QStringLiteral("从我喜欢的移除") : QStringLiteral("从播放列表移除"), this, &Client::removeSelected);
        if (!m_route.sort && playlistId != m_library->favoritesPlaylistId()) {
            menu.addAction(QStringLiteral("上移"), this, [this, row] { moveTrack(row, row - 1); });
            menu.addAction(QStringLiteral("下移"), this, [this, row] { moveTrack(row, row + 1); });
        }
    }
    menu.addSeparator(); menu.addAction(QStringLiteral("移到废纸篓…"), this, [this, ids] { trashTracks(ids); }); popup(menu);
}
void Client::cardMenu(int row)
{
    const auto card = m_cards.get(row); const auto kind = card.value("kind").toString();
    if (kind == "source") { sourceMenu(card.value("key").toLongLong()); return; }
    if (kind == "playlist") { playlistMenu(card.value("key").toLongLong()); return; }
    UiMenu menu; menu.addAction(QStringLiteral("播放全部"), this, [this, row] { playCard(row, false); });
    menu.addAction(QStringLiteral("随机播放"), this, [this, row] { playCard(row, true); }); popup(menu);
}
void Client::navigationMenu(int row)
{
    const auto item = m_navigation.get(row); if (item.value("protected").toBool()) return;
    if (item.value("kind") == "playlistSection") {
        UiMenu menu; menu.addAction(QStringLiteral("新建歌单…"), this, [this] { createPlaylist(); });
        menu.addAction(QStringLiteral("新建歌单文件夹…"), this, &Client::createFolder);
        menu.addAction(QStringLiteral("导入播放列表…"), this, &Client::importPlaylist); popup(menu);
    } else if (item.value("kind") == "playlist") playlistMenu(item.value("key").toLongLong());
    else if (item.value("kind") == "listFolder") folderMenu(item.value("key").toLongLong());
}
void Client::sourceMenu(qint64 id)
{
    const auto root = m_library->root(id); UiMenu menu;
    menu.addAction(QStringLiteral("打开"), this, [this, root] { navigate("source", QString::number(root.id), root.name); });
    menu.addAction(QStringLiteral("创建播放列表快照…"), this, [this, root] {
        askName(QStringLiteral("创建播放列表"), root.name, QStringLiteral("创建"), [this, root](const QString& name) {
            if (name.isEmpty()) return; QVector<qint64> ids;
            for (const auto& t : m_library->folderTracks(root.id)) ids << t.id;
            const auto playlist = m_library->createPlaylistFromFolder(root.id, name, ids);
            if (playlist) navigate("playlist", QString::number(playlist), name);
        });
    });
    menu.addAction(QStringLiteral("重新扫描"), this, [this, id] { m_library->refreshRoot(id); });
    menu.addAction(QStringLiteral("在文件管理器中显示"), this, [root] { showInFileManager(root.path); });
    menu.addAction(QStringLiteral("重新定位文件夹…"), this, [this, id] {
        choosePath(QStringLiteral("重新定位文件夹"), "folder", {}, [this, id](const QString& path) {
            QString error; if (!m_library->relocateRoot(id, path, &error)) report(error);
        });
    });
    menu.addSeparator(); menu.addAction(QStringLiteral("取消登记来源"), this, [this, id] {
        m_library->removeRoot(id); if (m_route.kind == "source" && m_route.key.toLongLong() == id) navigate("sources", {}, QStringLiteral("文件夹"));
    }); popup(menu);
}
void Client::playlistMenu(qint64 id)
{
    Playlist current; for (const auto& p : m_library->playlists()) if (p.id == id) current = p;
    if (!current.id || current.isFavorites()) return;
    UiMenu menu; menu.addAction(QStringLiteral("重命名…"), this, [this, current] {
        askName(QStringLiteral("重命名歌单"), current.name, QStringLiteral("完成"), [this, current](const QString& name) {
            if (!name.isEmpty()) m_library->renamePlaylist(current.id, name);
        });
    });
    auto* move = menu.addMenu(QStringLiteral("移动到"));
    move->addAction(QStringLiteral("顶层"), this, [this, id] { m_library->movePlaylistToFolder(id, 0); });
    for (const auto& f : m_library->playlistFolders()) move->addAction(f.name, this, [this, id, f] { m_library->movePlaylistToFolder(id, f.id); });
    menu.addAction(QStringLiteral("导出播放列表…"), this, [this, current] { navigate("playlist", QString::number(current.id), current.name); exportPlaylist(); });
    menu.addAction(QStringLiteral("删除歌单"), this, [this, id] {
        m_library->deletePlaylist(id); if (m_route.kind == "playlist" && m_route.key.toLongLong() == id) navigate("songs", {}, QStringLiteral("歌曲"));
    }); popup(menu);
}
void Client::folderMenu(qint64 id)
{
    PlaylistFolder current; for (const auto& f : m_library->playlistFolders()) if (f.id == id) current = f;
    UiMenu menu; menu.addAction(QStringLiteral("在此新建歌单"), this, [this, id] { createPlaylist(id); });
    menu.addAction(QStringLiteral("重命名…"), this, [this, current] {
        askName(QStringLiteral("重命名文件夹"), current.name, QStringLiteral("完成"), [this, current](const QString& name) {
            if (!name.isEmpty()) m_library->renamePlaylistFolder(current.id, name);
        });
    });
    menu.addAction(QStringLiteral("删除歌单文件夹"), this, [this, id] { m_library->deletePlaylistFolder(id); }); popup(menu);
}
void Client::action(const QString& name)
{
    if (name == "addSource") addMusicFolder();
    else if (name == "newPlaylist") createPlaylist();
    else if (name == "newFolder") createFolder();
    else if (name == "import") importPlaylist();
    else if (name == "export") exportPlaylist();
    else if (name == "settings") showSettings();
    else if (name == "mini") showMini();
    else if (name == "undo") m_library->undo();
    else if (name == "redo") m_library->redo();
    else if (name == "quit") quitApplication();
    else if (name == "refresh") m_library->rescanAll();
    else if (name == "selectAll") selectAll();
    else if (name == "menu") popup(*m_menuBar);
    else if (name == "pageMenu") {
        if (m_route.kind == "source") sourceMenu(m_route.key.toLongLong());
        else if (m_route.kind == "playlist") playlistMenu(m_route.key.toLongLong());
    } else if (name == "trash") trashTracks(selectedIds());
}
void Client::setupMenus()
{
    auto* file = m_menuBar->addMenu(QStringLiteral("文件"));
    file->addAction(QStringLiteral("添加音乐文件夹…"), this, &Client::addMusicFolder);
    file->addAction(QStringLiteral("导入播放列表…"), this, &Client::importPlaylist);
    file->addAction(QStringLiteral("导出播放列表…"), this, &Client::exportPlaylist);
    file->addAction(QStringLiteral("设置…"), this, &Client::showSettings);
    file->addAction(QStringLiteral("退出 Echo"), this, [] { quitApplication(); });
    auto* edit = m_menuBar->addMenu(QStringLiteral("编辑"));
    edit->addAction(QStringLiteral("撤销"), m_library, &Library::undo);
    edit->addAction(QStringLiteral("重做"), m_library, &Library::redo);
    edit->addAction(QStringLiteral("新建歌单…"), this, [this] { createPlaylist(); });
    edit->addAction(QStringLiteral("新建歌单文件夹…"), this, &Client::createFolder);
    auto* control = m_menuBar->addMenu(QStringLiteral("控制"));
    control->addAction(QStringLiteral("播放/暂停"), m_player, &Player::toggle);
    control->addAction(QStringLiteral("下一首"), m_player, &Player::next);
    control->addAction(QStringLiteral("上一首"), m_player, &Player::previous);
    control->addAction(QStringLiteral("迷你播放器"), this, &Client::showMini);
    auto* view = m_menuBar->addMenu(QStringLiteral("显示"));
    view->addAction(QStringLiteral("设置…"), this, &Client::showSettings);
}
}
