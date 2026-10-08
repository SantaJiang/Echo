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
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(800);
    connect(&m_rescanTimer, &QTimer::timeout, this, [this] {
        if (m_pendingRoot <= 0 || !m_database) {
            return;
        }
        for (const LibraryRoot& root : m_database->roots()) {
            if (root.id == m_pendingRoot) {
                scanRoot(root.id, root.path);
                break;
            }
        }
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (!m_database) {
            return;
        }
        for (const LibraryRoot& root : m_database->roots()) {
            if (path == root.path || path.startsWith(root.path + QLatin1Char('/'))) {
                m_pendingRoot = root.id;
                m_rescanTimer.start();
                break;
            }
        }
    });
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
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork");
    m_artwork = std::make_unique<ArtworkCache>(cache);
    m_scanThread = new QThread(this);
    m_scanner = new Scanner(m_database->filePath(), cache);
    m_scanner->moveToThread(m_scanThread);
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_scanner, &Scanner::progress, this, [this](qint64, int done, int total) { emit scanProgress(done, total); });
    connect(m_scanner, &Scanner::finished, this, [this](qint64) {
        watchRoots();
        emit changed();
        emit scanFinished();
    });
    connect(m_scanner, &Scanner::failed, this, &Library::scanFailed);
    m_scanThread->start();
    watchRoots();
    return true;
}

void Library::addRoot(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!QDir(absolute).exists() || !m_database) {
        return;
    }
    const qint64 id = m_database->addRoot(absolute);
    emit changed();
    scanRoot(id, absolute);
}

void Library::removeRoot(qint64 id)
{
    if (!m_database) {
        return;
    }
    m_database->removeRoot(id);
    watchRoots();
    emit changed();
}

QVector<LibraryRoot> Library::roots() const
{
    return m_database ? m_database->roots() : QVector<LibraryRoot> {};
}

void Library::rescanAll()
{
    for (const LibraryRoot& root : roots()) {
        scanRoot(root.id, root.path);
    }
}

QString Library::orderSql(const QString& column, bool descending)
{
    const QString safe = (column == QLatin1String("artist") || column == QLatin1String("album")
                          || column == QLatin1String("track_number") || column == QLatin1String("duration_ms"))
                             ? column
                             : QStringLiteral("title");
    return QStringLiteral("ORDER BY %1 %2, track_number, title").arg(safe, descending ? QStringLiteral("DESC") : QStringLiteral("ASC"));
}

QVector<Track> Library::allTracks(const QString& sortColumn, bool descending) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(QStringLiteral("WHERE missing=0"), {}, orderSql(sortColumn, descending));
}

QVector<Track> Library::albumTracks(const QString& albumKey) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(QStringLiteral("WHERE missing=0 AND album_key=?"), {albumKey},
                                   QStringLiteral("ORDER BY disc_number, track_number, title"));
}

QVector<Track> Library::artistTracks(const QString& artistKey) const
{
    if (!m_database) {
        return {};
    }
    return m_database->queryTracks(QStringLiteral("WHERE missing=0 AND artist_key=?"), {artistKey},
                                   QStringLiteral("ORDER BY album, disc_number, track_number"));
}

QVector<Track> Library::folderTracks(const QString& folder, bool recursive) const
{
    if (!m_database) {
        return {};
    }
    if (!recursive) {
        return m_database->queryTracks(QStringLiteral("WHERE missing=0 AND folder=?"), {folder},
                                       QStringLiteral("ORDER BY track_number, title"));
    }
    return m_database->queryTracks(QStringLiteral("WHERE missing=0 AND (folder=? OR folder LIKE ?)"),
                                   {folder, folder + QStringLiteral("/%")}, QStringLiteral("ORDER BY folder, track_number, title"));
}

QVector<Album> Library::albums(const QString& artistKey) const
{
    return m_database ? m_database->albums(artistKey) : QVector<Album> {};
}

QVector<Artist> Library::artists() const
{
    return m_database ? m_database->artists() : QVector<Artist> {};
}

QVector<QString> Library::folders() const
{
    return m_database ? m_database->folders() : QVector<QString> {};
}

Track Library::track(qint64 id) const
{
    return m_database ? m_database->track(id) : Track {};
}

QVector<Track> Library::tracks(const QVector<qint64>& ids) const
{
    return m_database ? m_database->tracksByIds(ids) : QVector<Track> {};
}

void Library::removeFromLibrary(const QVector<qint64>& ids)
{
    if (!m_database || ids.isEmpty()) {
        return;
    }
    m_database->removeTracks(ids);
    emit changed();
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
    if (m_database) {
        m_database->movePlaylist(id, folderId);
        emit changed();
    }
}

QVector<qint64> Library::playlistTrackIds(qint64 id) const
{
    return m_database ? m_database->playlistItems(id) : QVector<qint64> {};
}

void Library::addToPlaylist(qint64 playlistId, const QVector<qint64>& trackIds)
{
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
    QVector<qint64> removed;
    for (const int position : positions) {
        if (position >= 0 && position < items.size()) {
            removed.push_back(items[position]);
        }
    }
    m_database->removePlaylistItems(playlistId, positions);
    if (!m_applyingUndo) {
        UndoStep step;
        step.kind = UndoStep::Remove;
        step.playlistId = playlistId;
        step.trackIds = removed;
        step.positions = positions;
        remember(step);
    }
    emit changed();
}

void Library::movePlaylistItem(qint64 playlistId, int from, int to)
{
    if (!m_database || from == to) {
        return;
    }
    auto items = m_database->playlistItems(playlistId);
    if (from < 0 || from >= items.size() || to < 0 || to >= items.size()) {
        return;
    }
    const qint64 id = items.takeAt(from);
    items.insert(to, id);
    m_database->setPlaylistItems(playlistId, items);
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
    const UndoStep step = m_undo.takeLast();
    m_redo.push_back(step);
    apply(step, true);
}

void Library::redo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    const UndoStep step = m_redo.takeLast();
    m_undo.push_back(step);
    apply(step, false);
}

void Library::apply(const UndoStep& step, bool inverse)
{
    m_applyingUndo = true;
    switch (step.kind) {
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
            auto items = m_database->playlistItems(step.playlistId);
            QVector<int> positions = step.positions;
            std::sort(positions.begin(), positions.end());
            for (int index = 0; index < positions.size() && index < step.trackIds.size(); ++index) {
                const int at = std::clamp(positions[index], 0, static_cast<int>(items.size()));
                items.insert(at, step.trackIds[index]);
            }
            m_database->setPlaylistItems(step.playlistId, items);
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
}

bool Library::importM3u(qint64 playlistId, const QString& path, QString* error)
{
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
        QString resolved = line;
        if (QFileInfo(resolved).isRelative()) {
            resolved = QDir(directory).filePath(resolved);
        }
        resolved = QFileInfo(resolved).absoluteFilePath();
        qint64 matched = 0;
        for (const Track& candidate : m_database->queryTracks(QStringLiteral("WHERE path=? AND cue_start_ms<0"), {resolved}, {})) {
            matched = candidate.id;
            break;
        }
        if (matched == 0 && QFileInfo::exists(resolved) && isAudioFile(resolved)) {
            auto scanned = readTags(resolved);
            scanned.track.cueStartMs = -1;
            if (m_artwork) {
                scanned.track.artworkHash = m_artwork->store(scanned.artwork);
            }
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
        const QString resolved = info.absoluteFilePath();
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
    QMetaObject::invokeMethod(m_scanner, "scanRoot", Qt::QueuedConnection, Q_ARG(qint64, id), Q_ARG(QString, path));
}

void Library::watchRoots()
{
    const auto current = m_watcher.directories();
    if (!current.isEmpty()) {
        m_watcher.removePaths(current);
    }
    for (const LibraryRoot& root : roots()) {
        m_watcher.addPath(root.path);
#if !defined(Q_OS_MACOS)
        QDirIterator iterator(root.path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int added = 0;
        while (iterator.hasNext() && added < 4000) {
            m_watcher.addPath(iterator.next());
            ++added;
        }
#endif
    }
}

} // namespace echo
