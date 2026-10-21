#include "library/library.h"

#include "library/metadata.h"
#include "library/scanner.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QUrl>

namespace echo {
namespace {

QString containsPattern(const QString& text)
{
    return text.trimmed().toLower();
}

bool matches(const QString& haystack, const QString& needle)
{
    return haystack.contains(needle, Qt::CaseInsensitive);
}

QString ftsQuery(const QString& text)
{
    QStringList parts;
    const auto tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (QString token : tokens) {
        token.replace(u'"', u' ');
        token = token.trimmed();
        if (token.isEmpty()) {
            continue;
        }
        parts << QStringLiteral("\"%1\"*").arg(token);
    }
    return parts.join(u' ');
}

} // namespace

Library::Library(QObject* parent)
    : QObject(parent)
{
    connect(this, &Library::changed, this, &Library::refreshFavorites);
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(800);
    m_maxWaitTimer.setSingleShot(true);
    m_maxWaitTimer.setInterval(3000);
    m_verifyTimer.setInterval(5 * 60 * 1000);
    connect(&m_rescanTimer, &QTimer::timeout, this, &Library::startNextScan);
    connect(&m_maxWaitTimer, &QTimer::timeout, this, &Library::startNextScan);
    connect(&m_verifyTimer, &QTimer::timeout, this, &Library::rescanAll);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        for (const auto& root : roots())
            if (path == root.path) {
                m_pendingRoots.insert(root.id);
                m_rescanTimer.start();
                if (!m_maxWaitTimer.isActive())
                    m_maxWaitTimer.start();
            }
    });
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive)
                rescanAll();
        });
    }
}

Library::~Library()
{
    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
    }
}

bool Library::open(QString* error)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    m_database = std::make_unique<Database>();
    if (!m_database->open(base + QStringLiteral("/library.sqlite"), QStringLiteral("library"), error)) {
        return false;
    }
    refreshFavorites();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork");
    m_artwork = std::make_unique<ArtworkCache>(cache);
    m_scanThread = new QThread(this);
    m_scanner = new Scanner(m_database->filePath(), cache);
    m_scanner->moveToThread(m_scanThread);
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_scanner, &Scanner::progress, this, [this](qint64, int done, int total) { emit scanProgress(done, total); });
    connect(m_scanner, &Scanner::retryRequested, this,
        [this](qint64 id) { QTimer::singleShot(3000, this, [this, id] { refreshRoot(id); }); });
    connect(m_scanner, &Scanner::statistics, this, &Library::scanStatistics);
    connect(m_scanner, &Scanner::warning, this, &Library::scanWarning);
    connect(m_scanner, &Scanner::finished, this, [this](qint64, bool changed) { finishScan(changed); });
    connect(m_scanner, &Scanner::failed, this, [this](const QString& error) {
        emit scanFailed(error);
        finishScan();
    });
    m_scanThread->start();
    watchRoots();
    m_verifyTimer.start();
    QTimer::singleShot(0, this, &Library::rescanAll);
    return true;
}

qint64 Library::addRoot(const QString& path)
{
    const QString absolute = QFileInfo(path).canonicalFilePath();
    if (absolute.isEmpty() || !QFileInfo(absolute).isDir() || !m_database)
        return 0;
    for (const auto& existing : roots())
        if (existing.path == absolute) {
            refreshRoot(existing.id);
            return existing.id;
        }
    const qint64 id = m_database->addRoot(absolute);
    watchRoots();
    emit sourceChanged(id);
    emit changed();
    refreshRoot(id);
    return id;
}

void Library::removeRoot(qint64 id)
{
    if (!m_database)
        return;
    m_pendingRoots.remove(id);
    m_database->removeRoot(id);
    watchRoots();
    emit sourceChanged(id);
    emit changed();
}

LibraryRoot Library::root(qint64 id) const
{
    return m_database ? m_database->root(id) : LibraryRoot { };
}
QVector<LibraryRoot> Library::roots() const
{
    return m_database ? m_database->roots() : QVector<LibraryRoot> { };
}

void Library::refreshRoot(qint64 id)
{
    const auto source = root(id);
    if (!source.id || !source.active)
        return;
    m_pendingRoots.insert(id);
    if (!m_scanning)
        startNextScan();
}

void Library::rescanAll()
{
    refreshLooseFiles();
    for (const auto& source : roots())
        m_pendingRoots.insert(source.id);
    if (!m_scanning)
        startNextScan();
}

void Library::refreshLooseFiles()
{
    if (!m_database || m_checkingLoose)
        return;
    auto loose = m_database->queryTracks(
        QStringLiteral("WHERE NOT EXISTS (SELECT 1 FROM roots r WHERE r.id=tracks.root_id AND r.active=1)"), { }, { });
    if (loose.isEmpty())
        return;
    m_checkingLoose = true;
    auto* watcher = new QFutureWatcher<QVector<Track>>(this);
    connect(watcher, &QFutureWatcher<QVector<Track>>::finished, this, [this, watcher] {
        m_checkingLoose = false;
        if (m_database->updateLooseAvailability(watcher->result()))
            emit changed();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([loose]() mutable {
        for (auto& track : loose) {
            const QFileInfo file(track.path);
            track.missing = !file.isFile() || !file.isReadable()
                || (!track.cuePath.isEmpty() && track.cuePath != track.path && !QFileInfo::exists(track.cuePath));
        }
        return loose;
    }));
}

void Library::startNextScan()
{
    if (m_scanning || !m_scanner || m_pendingRoots.isEmpty())
        return;
    m_rescanTimer.stop();
    m_maxWaitTimer.stop();
    auto pending = m_pendingRoots.values();
    std::sort(pending.begin(), pending.end());
    const auto next = std::upper_bound(pending.begin(), pending.end(), m_lastServedRoot);
    const auto id = next == pending.end() ? pending.front() : *next;
    m_lastServedRoot = id;
    m_pendingRoots.remove(id);
    const auto source = root(id);
    if (!source.id || !source.active) {
        QTimer::singleShot(0, this, &Library::startNextScan);
        return;
    }
    scanRoot(id, source.path);
}

void Library::finishScan(bool changed)
{
    const auto id = m_activeRoot;
    m_scanning = false;
    m_activeRoot = 0;
    watchRoots();
    if (changed)
        emit sourceChanged(id);
    emit scanFinished();
    QTimer::singleShot(0, this, &Library::startNextScan);
}

bool Library::relocateRoot(qint64 id, const QString& path, QString* error)
{
    const auto source = root(id);
    const QString target = QFileInfo(path).canonicalFilePath();
    if (!source.id || !QFileInfo(target).isDir()) {
        if (error)
            *error = QStringLiteral("请选择存在的文件夹");
        return false;
    }
    for (const auto& other : roots())
        if (other.id != id && other.path == target) {
            if (error)
                *error = QStringLiteral("目标文件夹已添加，请选择其他目录");
            return false;
        }
    // Check every match and unique-key conflict before moving the source. Do not guess by song title.
    QVector<QPair<qint64, QString>> matches;
    QStringList conflicts;
    for (const auto& track : folderTracks(id)) {
        const QString candidate = QDir(target).filePath(QFileInfo(track.path).fileName());
        const QFileInfo info(candidate);
        if (!info.isFile() || info.size() != track.fileSize || info.lastModified().toMSecsSinceEpoch() != track.mtime)
            continue;
        bool conflict = false;
        for (const auto& existing : tracksAtPath(candidate))
            if (existing.id != track.id && existing.cueStartMs == track.cueStartMs)
                conflict = true;
        if (conflict)
            conflicts << candidate;
        else
            matches.push_back({ track.id, candidate });
    }
    if (!conflicts.isEmpty()) {
        if (error)
            *error = QStringLiteral("目标已有歌曲记录，未更改：\n") + conflicts.join(u'\n');
        return false;
    }
    if (!m_database->updateRoot(id, target, matches, error))
        return false;
    watchRoots();
    emit sourceChanged(id);
    emit changed();
    refreshRoot(id);
    return true;
}

bool Library::relocateTrack(qint64 id, const QString& path, QString* error)
{
    const QString absolute = normalizedMusicPath(path);
    if (!QFileInfo(absolute).isFile() || !isAudioFile(absolute)) {
        if (error)
            *error = QStringLiteral("请选择存在的音乐文件");
        return false;
    }
    const auto previous = track(id);
    if (!previous.id) {
        if (error)
            *error = QStringLiteral("找不到原歌曲记录");
        return false;
    }
    auto tags = readTags(absolute);
    if (!tags.ok) {
        if (error)
            *error = QStringLiteral("无法读取目标音乐文件");
        return false;
    }
    if (!m_database->relocateTrack(id, absolute, error)) {
        if (error && error->contains(QStringLiteral("UNIQUE")))
            *error = QStringLiteral("目标文件已有歌曲记录，未更改原有引用。请选择其他文件。");
        return false;
    }
    const auto moved = track(id);
    if (previous.cueStartMs < 0) {
        tags.track.rootId = moved.rootId;
        tags.track.artworkHash = m_artwork->store(tags.artwork);
        m_database->upsertTrack(tags.track);
    }
    emit changed();
    const auto updated = track(id);
    if (updated.rootId)
        refreshRoot(updated.rootId);
    return true;
}

QVector<Track> Library::tracksAtPath(const QString& path) const
{
    return m_database->queryTracks(QStringLiteral("WHERE path=?"), { normalizedMusicPath(path) }, { });
}

void Library::recheckTrack(qint64 id)
{
    const auto item = track(id);
    if (item.rootId && root(item.rootId).active) {
        refreshRoot(item.rootId);
        return;
    }
    if (!QFileInfo(item.path).isFile()) {
        m_database->markPathMissing(item.path);
        emit changed();
        return;
    }
    auto updated = item;
    updated.missing = false;
    m_database->upsertTrack(updated);
    emit changed();
}

void Library::markMissingPath(const QString& path)
{
    m_database->markPathMissing(normalizedMusicPath(path));
    const auto parent = QFileInfo(path).absolutePath();
    for (const auto& source : roots())
        if (source.path == parent)
            refreshRoot(source.id);
    emit changed();
}

bool Library::trashFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.moveToTrash()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_database->markPathMissing(normalizedMusicPath(path));
    for (const auto& source : roots())
        if (source.path == QFileInfo(path).absolutePath())
            refreshRoot(source.id);
    emit changed();
    return true;
}

qint64 Library::createPlaylistFromFolder(qint64 rootId, const QString& name, const QVector<qint64>& ids)
{
    if (!root(rootId).id || name.trimmed().isEmpty() || ids.isEmpty())
        return 0;
    const qint64 id = m_database->createPlaylistWithItems(name.trimmed(), ids);
    if (id) {
        UndoStep step { };
        step.kind = UndoStep::SnapshotPlaylist;
        step.playlistId = id;
        step.name = name.trimmed();
        step.trackIds = ids;
        remember(step);
        emit changed();
    }
    return id;
}

QString Library::orderSql(const QString& column, bool descending)
{
    const QString safe = (column == QLatin1String("artist") || column == QLatin1String("album")
                          || column == QLatin1String("track_number") || column == QLatin1String("duration_ms"))
                             ? column
                             : QStringLiteral("title");
    return QStringLiteral("ORDER BY %1 %2, disc_number, track_number, title, id")
        .arg(safe, descending ? QStringLiteral("DESC") : QStringLiteral("ASC"));
}

QVector<Track> Library::allTracks(const QString& sortColumn, bool descending) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(
        QStringLiteral("WHERE ") + Database::libraryScope(), { }, orderSql(sortColumn, descending));
}

QVector<Track> Library::albumTracks(const QString& albumKey) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(
        QStringLiteral("WHERE ") + Database::libraryScope() + QStringLiteral(" AND album_key=?"), { albumKey },
        QStringLiteral("ORDER BY disc_number, track_number, title"));
}

QVector<Track> Library::artistTracks(const QString& artistKey) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(
        QStringLiteral("WHERE ") + Database::libraryScope() + QStringLiteral(" AND artist_key=?"), { artistKey },
        QStringLiteral("ORDER BY album, disc_number, track_number"));
}

QVector<Track> Library::folderTracks(qint64 rootId) const
{
    if (!m_database)
        return { };
    // Retain cached records after a move, including unmatched files needing relocation.
    return m_database->queryTracks(QStringLiteral("WHERE root_id=?"), { rootId },
        QStringLiteral("ORDER BY album, disc_number, track_number, title, id"));
}

QVector<Album> Library::albums(const QString& artistKey) const
{
    return m_database ? m_database->albums(artistKey) : QVector<Album> {};
}

QVector<Artist> Library::artists() const
{
    return m_database ? m_database->artists() : QVector<Artist> {};
}

Track Library::track(qint64 id) const
{
    return m_database ? m_database->track(id) : Track {};
}

QVector<Track> Library::tracks(const QVector<qint64>& ids) const
{
    return m_database ? m_database->tracksByIds(ids) : QVector<Track> {};
}

bool Library::saveTags(qint64 id, const TagEdit& edit, QString* error)
{
    const Track current = track(id);
    if (current.id == 0) {
        if (error) {
            *error = QStringLiteral("找不到曲目");
        }
        return false;
    }
    if (!current.cuePath.isEmpty() && current.cueNumber > 0 && QFileInfo(current.cuePath).suffix().compare(u"cue", Qt::CaseInsensitive) == 0) {
        if (!rewriteCueEntry(current.cuePath, current.cueNumber, edit.title, edit.artist, error)) {
            return false;
        }
        if (edit.editLyrics) {
            const QFileInfo audio(current.path);
            QFile lyricsFile(audio.absolutePath() + QLatin1Char('/') + audio.completeBaseName() + QStringLiteral(".lrc"));
            if (lyricsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                lyricsFile.write(edit.lyrics.toUtf8());
            }
        }
    } else if (!writeTags(current.path, edit, error)) {
        return false;
    }
    Track updated = current;
    updated.title = edit.title;
    updated.artist = edit.artist;
    updated.album = edit.album;
    updated.albumArtist = edit.albumArtist;
    updated.trackNumber = edit.trackNumber;
    updated.year = edit.year;
    if (edit.editLyrics) {
        updated.lyrics = edit.lyrics;
    }
    if (edit.editArtwork && m_artwork) {
        updated.artworkHash = m_artwork->store(edit.artwork);
    }
    updated.albumKey = makeAlbumKey(updated.albumArtist, updated.artist, updated.album);
    updated.artistKey = makeArtistKey(updated.artist);
    m_database->upsertTrack(updated);
    emit changed();
    return true;
}

QVector<Track> Library::searchTracks(const QString& text) const
{
    if (!m_database) {
        return {};
    }
    const QString query = ftsQuery(text);
    if (m_database->hasFts() && !query.isEmpty()) {
        const auto found = m_database->searchTracks(query, 200);
        if (!found.isEmpty() || text.trimmed().size() > 1) {
            return found;
        }
    }
    const QString needle = containsPattern(text);
    QVector<Track> result;
    for (const Track& track : allTracks(QStringLiteral("title"), false)) {
        if (matches(track.title, needle) || matches(track.artist, needle) || matches(track.album, needle)) {
            result.push_back(track);
        }
        if (result.size() >= 200) {
            break;
        }
    }
    return result;
}

QVector<Album> Library::searchAlbums(const QString& text) const
{
    QVector<Album> result;
    for (const Album& album : albums()) {
        if (matches(album.title, text) || matches(album.artist, text)) {
            result.push_back(album);
        }
    }
    return result;
}

QVector<Artist> Library::searchArtists(const QString& text) const
{
    QVector<Artist> result;
    for (const Artist& artist : artists()) {
        if (matches(artist.name, text)) {
            result.push_back(artist);
        }
    }
    return result;
}

QVector<Playlist> Library::searchPlaylists(const QString& text) const
{
    QVector<Playlist> result;
    for (const Playlist& playlist : playlists()) {
        if (matches(playlist.name, text)) {
            result.push_back(playlist);
        }
    }
    return result;
}

qint64 Library::favoritesPlaylistId() const
{
    return m_database ? m_database->favoritesPlaylistId() : 0;
}

void Library::refreshFavorites()
{
    const auto ids = playlistTrackIds(favoritesPlaylistId());
    const QSet<qint64> updated(ids.begin(), ids.end());
    if (updated == m_favorites) return;
    m_favorites = updated;
    emit favoritesChanged();
}

bool Library::setFavorites(const QVector<qint64>& ids, bool liked)
{
    if (!m_database) return false;
    const auto before = playlistTrackIds(favoritesPlaylistId());
    bool needsChange = false;
    for (qint64 id : ids) {
        if (id > 0 && (liked ? !isFavorite(id) && track(id).id : isFavorite(id))) needsChange = true;
    }
    if (!needsChange) return true;
    QString error;
    if (!m_database->setFavorites(ids, liked, &error)) {
        emit favoritesFailed(QStringLiteral("无法更新我喜欢的：%1").arg(error));
        return false;
    }
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::Favorites;
        step.beforeFavorites = before;
        step.afterFavorites = playlistTrackIds(favoritesPlaylistId());
        remember(step);
    }
    refreshFavorites();
    return true;
}

QVector<Playlist> Library::playlists() const
{
    return m_database ? m_database->playlists() : QVector<Playlist> {};
}

QVector<PlaylistFolder> Library::playlistFolders() const
{
    return m_database ? m_database->playlistFolders() : QVector<PlaylistFolder> {};
}

qint64 Library::createPlaylist(const QString& name, qint64 folderId)
{
    if (!m_database) {
        return 0;
    }
    const qint64 id = m_database->createPlaylist(name, folderId);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::CreatePlaylist;
        step.playlistId = id;
        step.name = name;
        step.folderId = folderId;
        remember(step);
    }
    emit changed();
    return id;
}

qint64 Library::createPlaylistFolder(const QString& name, qint64 parentId)
{
    if (!m_database) {
        return 0;
    }
    const qint64 id = m_database->createPlaylistFolder(name, parentId);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::CreateFolder;
        step.folderId = id;
        step.name = name;
        remember(step);
    }
    emit changed();
    return id;
}

void Library::renamePlaylist(qint64 id, const QString& name)
{
    if (id == favoritesPlaylistId()) return;
    if (!m_database) {
        return;
    }
    QString previous;
    for (const Playlist& playlist : playlists()) {
        if (playlist.id == id) {
            previous = playlist.name;
        }
    }
    m_database->renamePlaylist(id, name);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::RenamePlaylist;
        step.playlistId = id;
        step.name = name;
        step.previousName = previous;
        remember(step);
    }
    emit changed();
}

void Library::renamePlaylistFolder(qint64 id, const QString& name)
{
    if (!m_database) {
        return;
    }
    QString previous;
    for (const PlaylistFolder& folder : playlistFolders()) {
        if (folder.id == id) {
            previous = folder.name;
        }
    }
    m_database->renamePlaylistFolder(id, name);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::RenameFolder;
        step.folderId = id;
        step.name = name;
        step.previousName = previous;
        remember(step);
    }
    emit changed();
}

void Library::deletePlaylist(qint64 id)
{
    if (id == favoritesPlaylistId()) return;
    if (!m_database) {
        return;
    }
    QString name;
    qint64 folderId = 0;
    for (const Playlist& playlist : playlists()) {
        if (playlist.id == id) {
            name = playlist.name;
            folderId = playlist.folderId;
        }
    }
    const auto items = m_database->playlistItems(id);
    m_database->deletePlaylist(id);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::DeletePlaylist;
        step.playlistId = id;
        step.folderId = folderId;
        step.name = name;
        step.trackIds = items;
        remember(step);
    }
    emit changed();
}

void Library::deletePlaylistFolder(qint64 id)
{
    if (m_database) {
        m_database->deletePlaylistFolder(id);
        emit changed();
    }
}

void Library::movePlaylistToFolder(qint64 id, qint64 folderId)
{
    if (id == favoritesPlaylistId()) return;
    if (m_database) {
        m_database->movePlaylist(id, folderId);
        emit changed();
    }
}

QVector<Track> Library::playlistTracks(qint64 id) const
{
    QSqlQuery query(m_database->m_db);
    query.prepare(QStringLiteral("SELECT id,track_id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id"));
    query.addBindValue(id);
    query.exec();
    QVector<qint64> ids, entries;
    while (query.next()) {
        entries << query.value(0).toLongLong();
        ids << query.value(1).toLongLong();
    }
    auto result = tracks(ids);
    for (int i = 0; i < result.size(); ++i) {
        result[i].listEntryId = entries[i];
        result[i].playlistPosition = i;
    }
    return result;
}

QVector<qint64> Library::playlistTrackIds(qint64 id) const
{
    return m_database ? m_database->playlistItems(id) : QVector<qint64> {};
}

void Library::addToPlaylist(qint64 playlistId, const QVector<qint64>& trackIds)
{
    if (playlistId == favoritesPlaylistId()) { setFavorites(trackIds, true); return; }
    if (!m_database || trackIds.isEmpty()) {
        return;
    }
    const int index = m_database->playlistItems(playlistId).size();
    m_database->insertPlaylistItems(playlistId, index, trackIds);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::Add;
        step.playlistId = playlistId;
        step.trackIds = trackIds;
        step.from = index;
        remember(step);
    }
    emit changed();
}

void Library::removePlaylistPositions(qint64 playlistId, const QVector<int>& positions)
{
    if (!m_database || positions.isEmpty()) {
        return;
    }
    const auto items = m_database->playlistItems(playlistId);
    QVector<int> ordered = positions;
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    ordered.erase(std::remove_if(ordered.begin(), ordered.end(), [&](int i) { return i < 0 || i >= items.size(); }),
        ordered.end());
    QVector<qint64> removed;
    for (const int position : ordered)
        removed.push_back(items[position]);
    if (playlistId == favoritesPlaylistId()) { setFavorites(removed, false); return; }
    m_database->removePlaylistItems(playlistId, ordered);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::Remove;
        step.playlistId = playlistId;
        step.trackIds = removed;
        step.positions = ordered;
        remember(step);
    }
    emit changed();
}

void Library::movePlaylistItem(qint64 playlistId, int from, int to)
{
    if (playlistId == favoritesPlaylistId()) return;
    if (!m_database || from == to) {
        return;
    }
    auto items = m_database->playlistItems(playlistId);
    if (from < 0 || from >= items.size() || to < 0 || to >= items.size()) {
        return;
    }
    const qint64 id = items.takeAt(from);
    items.insert(to, id);
    m_database->reorderPlaylist(playlistId, from, to);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::Move;
        step.playlistId = playlistId;
        step.from = from;
        step.to = to;
        remember(step);
    }
    emit changed();
}

void Library::remember(const UndoStep& step)
{
    m_undo.push_back(step);
    if (m_undo.size() > 50) {
        m_undo.removeFirst();
    }
    m_redo.clear();
}

void Library::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    UndoStep step = m_undo.last();
    if (!apply(step, true)) return;
    m_undo.removeLast();
    m_redo.push_back(step);
}

void Library::redo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    UndoStep step = m_redo.last();
    if (!apply(step, false)) return;
    m_redo.removeLast();
    m_undo.push_back(step);
}

bool Library::apply(UndoStep& step, bool inverse)
{
    m_applyingUndo = true;
    switch (step.kind) {
    case UndoStep::SnapshotPlaylist:
        if (inverse)
            m_database->deletePlaylist(step.playlistId);
        else {
            step.playlistId = m_database->createPlaylistWithItems(step.name, step.trackIds);
            if (!step.playlistId) {
                m_applyingUndo = false;
                return false;
            }
        }
        emit changed();
        break;
    case UndoStep::Favorites: {
        QString error;
        if (!m_database->restoreFavorites(inverse ? step.beforeFavorites : step.afterFavorites, &error)) {
            m_applyingUndo = false;
            emit favoritesFailed(QStringLiteral("无法更新我喜欢的：%1").arg(error));
            return false;
        }
        refreshFavorites();
        break;
    }
    case UndoStep::Add:
        if (inverse) {
            QVector<int> positions;
            for (int index = 0; index < step.trackIds.size(); ++index) {
                positions.push_back(step.from + index);
            }
            removePlaylistPositions(step.playlistId, positions);
        } else {
            m_database->insertPlaylistItems(step.playlistId, step.from, step.trackIds);
            emit changed();
        }
        break;
    case UndoStep::Remove:
        if (inverse) {
            auto positions = step.positions;
            std::sort(positions.begin(), positions.end());
            for (int i = 0; i < positions.size() && i < step.trackIds.size(); ++i)
                m_database->insertPlaylistItems(step.playlistId, positions[i], { step.trackIds[i] });
            emit changed();
        } else {
            removePlaylistPositions(step.playlistId, step.positions);
        }
        break;
    case UndoStep::Move:
        movePlaylistItem(step.playlistId, inverse ? step.to : step.from, inverse ? step.from : step.to);
        break;
    case UndoStep::RenamePlaylist:
        renamePlaylist(step.playlistId, inverse ? step.previousName : step.name);
        break;
    case UndoStep::RenameFolder:
        renamePlaylistFolder(step.folderId, inverse ? step.previousName : step.name);
        break;
    case UndoStep::DeletePlaylist:
        if (inverse) {
            const qint64 id = createPlaylist(step.name, step.folderId);
            m_database->setPlaylistItems(id, step.trackIds);
            emit changed();
        } else if (step.playlistId > 0) {
            deletePlaylist(step.playlistId);
        }
        break;
    case UndoStep::CreatePlaylist:
        if (inverse) {
            deletePlaylist(step.playlistId);
        }
        break;
    case UndoStep::CreateFolder:
        if (inverse) {
            deletePlaylistFolder(step.folderId);
        }
        break;
    }
    m_applyingUndo = false;
    return true;
}

bool Library::importM3u(qint64 playlistId, const QString& path, QString* error)
{
    if (playlistId == favoritesPlaylistId()) {
        if (error) *error = QStringLiteral("请将播放列表导入普通歌单");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法读取播放列表");
        }
        return false;
    }
    const QString directory = QFileInfo(path).absolutePath();
    QVector<qint64> ids;
    const auto lines = QString::fromUtf8(file.readAll()).split(QRegularExpression(QStringLiteral("[\r\n]+")));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        const QUrl url(line);
        if (!url.scheme().isEmpty() && !url.isLocalFile() && !QDir::isAbsolutePath(line))
            continue;
        QString resolved = url.isLocalFile() ? url.toLocalFile() : line;
        if (QFileInfo(resolved).isRelative()) {
            resolved = QDir(directory).filePath(resolved);
        }
        resolved = normalizedMusicPath(resolved);
        qint64 matched = 0;
        for (const Track& candidate : m_database->queryTracks(QStringLiteral("WHERE path=? AND cue_start_ms<0"), {resolved}, {})) {
            matched = candidate.id;
            break;
        }
        if (matched == 0 && isAudioFile(resolved)) {
            auto scanned = readTags(resolved);
            scanned.track.cueStartMs = -1;
            if (m_artwork) {
                scanned.track.artworkHash = m_artwork->store(scanned.artwork);
            }
            scanned.track.missing = !QFileInfo::exists(resolved);
            // Existing direct children can join their explicitly registered source; other files stay standalone.
            for (const auto& source : roots())
                if (source.path == scanned.track.folder)
                    scanned.track.rootId = source.id;
            matched = m_database->upsertTrack(scanned.track);
        }
        if (matched > 0) {
            ids.push_back(matched);
        }
    }
    addToPlaylist(playlistId, ids);
    return true;
}

QVector<qint64> Library::importFiles(const QStringList& paths)
{
    QVector<qint64> ids;
    if (!m_database) {
        return ids;
    }
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            addRoot(info.absoluteFilePath());
            continue;
        }
        if (!info.exists() || !isAudioFile(info.absoluteFilePath())) {
            continue;
        }
        const QString resolved = normalizedMusicPath(path);
        qint64 matched = 0;
        for (const Track& candidate : m_database->queryTracks(QStringLiteral("WHERE path=? AND cue_start_ms<0"), {resolved}, {})) {
            matched = candidate.id;
            break;
        }
        if (matched == 0) {
            auto scanned = readTags(resolved);
            scanned.track.cueStartMs = -1;
            if (m_artwork) {
                scanned.track.artworkHash = m_artwork->store(scanned.artwork);
            }
            scanned.track.missing = !QFileInfo::exists(resolved);
            // Existing direct children can join their explicitly registered source; other files stay standalone.
            for (const auto& source : roots())
                if (source.path == scanned.track.folder)
                    scanned.track.rootId = source.id;
            matched = m_database->upsertTrack(scanned.track);
        }
        if (matched > 0) {
            ids.push_back(matched);
        }
    }
    if (!ids.isEmpty()) {
        emit changed();
    }
    return ids;
}

bool Library::exportM3u(qint64 playlistId, const QString& path, QString* error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法写入播放列表");
        }
        return false;
    }
    QString body = QStringLiteral("#EXTM3U\n");
    for (const qint64 id : playlistTrackIds(playlistId)) {
        const Track item = track(id);
        body += QStringLiteral("#EXTINF:%1,%2 - %3\n%4\n")
                    .arg(item.durationMs / 1000)
                    .arg(item.displayArtist(), item.displayTitle(), item.path);
    }
    file.write(body.toUtf8());
    return file.commit();
}

QString Library::setting(const QString& key, const QString& fallback) const
{
    return m_database ? m_database->setting(key, fallback) : fallback;
}

void Library::setSetting(const QString& key, const QString& value)
{
    if (m_database) {
        m_database->setSetting(key, value);
    }
}

void Library::scanRoot(qint64 id, const QString& path)
{
    if (!m_scanner) {
        return;
    }
    m_scanning = true;
    m_activeRoot = id;
    emit scanStarted(id);
    QMetaObject::invokeMethod(m_scanner, "scanRoot", Qt::QueuedConnection, Q_ARG(qint64, id), Q_ARG(QString, path));
}

void Library::watchRoots()
{
    QStringList wanted;
    for (const auto& source : roots())
        wanted << source.path;
    const auto current = m_watcher.directories();
    for (const auto& path : current)
        if (!wanted.contains(path))
            m_watcher.removePath(path);
    for (const auto& path : wanted)
        if (!current.contains(path))
            m_watcher.addPath(path);
}

} // namespace echo
