#include "library/database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSqlError>
#include <QVariant>

#include <algorithm>

namespace echo {
namespace {

const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS roots (
  id INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE
);
CREATE TABLE IF NOT EXISTS tracks (
  id INTEGER PRIMARY KEY,
  root_id INTEGER,
  path TEXT NOT NULL,
  folder TEXT,
  title TEXT,
  artist TEXT,
  album_artist TEXT,
  album TEXT,
  album_key TEXT,
  artist_key TEXT,
  year INTEGER,
  track_number INTEGER,
  disc_number INTEGER,
  duration_ms INTEGER,
  sample_rate INTEGER,
  bit_depth INTEGER,
  channels INTEGER,
  codec TEXT,
  has_track_gain INTEGER,
  has_album_gain INTEGER,
  track_gain_db REAL,
  album_gain_db REAL,
  track_peak REAL,
  album_peak REAL,
  lyrics TEXT,
  artwork_hash TEXT,
  mtime INTEGER,
  file_size INTEGER,
  missing INTEGER NOT NULL DEFAULT 0,
  cue_path TEXT,
  cue_number INTEGER,
  cue_start_ms INTEGER NOT NULL DEFAULT -1,
  cue_end_ms INTEGER,
  added_at INTEGER NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_tracks_identity ON tracks(path, cue_start_ms);
CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album_key);
CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist_key);
CREATE INDEX IF NOT EXISTS idx_tracks_folder ON tracks(folder);
CREATE INDEX IF NOT EXISTS idx_tracks_root ON tracks(root_id);
CREATE TABLE IF NOT EXISTS playlist_folders (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  parent_id INTEGER,
  sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS playlists (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  folder_id INTEGER,
  sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS playlist_items (
  id INTEGER PRIMARY KEY,
  playlist_id INTEGER NOT NULL,
  track_id INTEGER NOT NULL,
  sort_order INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT
);
)SQL";

} // namespace

QMutex& Database::writeLock()
{
    static QMutex mutex;
    return mutex;
}

bool Database::open(const QString& filePath, const QString& connectionName, QString* error)
{
#ifdef ECHO_QT_PLUGINS
    QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
#endif
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    }
    m_db.setDatabaseName(filePath);
    if (!m_db.open()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }
    m_path = filePath;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    const QString schema = QString::fromUtf8(kSchema);
    for (const QString& statement : schema.split(u';', Qt::SkipEmptyParts)) {
        if (statement.trimmed().isEmpty()) {
            continue;
        }
        if (!query.exec(statement)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }
    // Older libraries have no system_key. Migrate atomically on either connection.
    {
        QMutexLocker locker(&writeLock());
        if (!m_db.transaction()) return false;
        auto fail = [&] {
            if (error) *error = query.lastError().text();
            m_db.rollback();
            return false;
        };
        bool hasSystemKey = false;
        query.exec(QStringLiteral("PRAGMA table_info(playlists)"));
        while (query.next()) hasSystemKey |= query.value(1).toString() == QLatin1String("system_key");
        if (!hasSystemKey && !query.exec(QStringLiteral("ALTER TABLE playlists ADD COLUMN system_key TEXT"))) return fail();
        const QStringList statements {
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_playlist_system_key ON playlists(system_key)"),
            QStringLiteral("INSERT OR IGNORE INTO playlists(name, sort_order, system_key) VALUES ('我喜欢的', 0, 'favorites')"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS tracks_cleanup_playlists AFTER DELETE ON tracks BEGIN DELETE FROM playlist_items WHERE track_id=old.id; END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS protect_system_playlist_delete BEFORE DELETE ON playlists WHEN old.system_key IS NOT NULL BEGIN SELECT RAISE(ABORT, 'System playlist is protected'); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS protect_system_playlist_update BEFORE UPDATE ON playlists WHEN old.system_key IS NOT NULL BEGIN SELECT RAISE(ABORT, 'System playlist is protected'); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS unique_favorite BEFORE INSERT ON playlist_items WHEN new.playlist_id=(SELECT id FROM playlists WHERE system_key='favorites') AND EXISTS(SELECT 1 FROM playlist_items WHERE playlist_id=new.playlist_id AND track_id=new.track_id) BEGIN SELECT RAISE(ABORT, 'Duplicate favorite'); END")
        };
        for (const auto& statement : statements) if (!query.exec(statement)) return fail();
        if (!m_db.commit()) return fail();
    }
    m_fts = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5("
        "title, artist, album_artist, album, folder, content='tracks', content_rowid='id', "
        "tokenize='unicode61 remove_diacritics 2')"));
    if (m_fts) {
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN "
            "INSERT INTO tracks_fts(rowid, title, artist, album_artist, album, folder) "
            "VALUES (new.id, new.title, new.artist, new.album_artist, new.album, new.folder); END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN "
            "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album_artist, album, folder) "
            "VALUES ('delete', old.id, old.title, old.artist, old.album_artist, old.album, old.folder); END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN "
            "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album_artist, album, folder) "
            "VALUES ('delete', old.id, old.title, old.artist, old.album_artist, old.album, old.folder); "
            "INSERT INTO tracks_fts(rowid, title, artist, album_artist, album, folder) "
            "VALUES (new.id, new.title, new.artist, new.album_artist, new.album, new.folder); END"));
    }
    return true;
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

qint64 Database::addRoot(const QString& path)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO roots(path) VALUES (?)"));
    query.addBindValue(path);
    query.exec();
    query.prepare(QStringLiteral("SELECT id FROM roots WHERE path=?"));
    query.addBindValue(path);
    query.exec();
    return query.next() ? query.value(0).toLongLong() : 0;
}

void Database::removeRoot(qint64 id)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tracks WHERE root_id=?"));
    query.addBindValue(id);
    query.exec();
    query.prepare(QStringLiteral("DELETE FROM roots WHERE id=?"));
    query.addBindValue(id);
    query.exec();
}

QVector<LibraryRoot> Database::roots() const
{
    QVector<LibraryRoot> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id, path FROM roots ORDER BY path"));
    while (query.next()) {
        result.push_back(LibraryRoot {query.value(0).toLongLong(), query.value(1).toString()});
    }
    return result;
}

void Database::markRootMissing(qint64 rootId)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tracks SET missing=1 WHERE root_id=?"));
    query.addBindValue(rootId);
    query.exec();
}

qint64 Database::upsertTrack(const Track& track)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM tracks WHERE path=? AND cue_start_ms=?"));
    query.addBindValue(track.path);
    query.addBindValue(track.cueStartMs);
    query.exec();
    const bool exists = query.next();
    const qint64 existingId = exists ? query.value(0).toLongLong() : 0;
    if (exists) {
        query.prepare(QStringLiteral(
            "UPDATE tracks SET root_id=?, folder=?, title=?, artist=?, album_artist=?, album=?, album_key=?, artist_key=?, "
            "year=?, track_number=?, disc_number=?, duration_ms=?, sample_rate=?, bit_depth=?, channels=?, codec=?, "
            "has_track_gain=?, has_album_gain=?, track_gain_db=?, album_gain_db=?, track_peak=?, album_peak=?, lyrics=?, "
            "artwork_hash=?, mtime=?, file_size=?, missing=0, cue_path=?, cue_number=?, cue_end_ms=? WHERE id=?"));
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO tracks(root_id, path, folder, title, artist, album_artist, album, album_key, artist_key, year, "
            "track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, has_track_gain, has_album_gain, "
            "track_gain_db, album_gain_db, track_peak, album_peak, lyrics, artwork_hash, mtime, file_size, missing, cue_path, "
            "cue_number, cue_start_ms, cue_end_ms, added_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0,?,?,?,?,?)"));
    }
    query.addBindValue(track.rootId > 0 ? QVariant(track.rootId) : QVariant());
    if (!exists) {
        query.addBindValue(track.path);
    }
    query.addBindValue(track.folder);
    query.addBindValue(track.title);
    query.addBindValue(track.artist);
    query.addBindValue(track.albumArtist);
    query.addBindValue(track.album);
    query.addBindValue(track.albumKey);
    query.addBindValue(track.artistKey);
    query.addBindValue(track.year);
    query.addBindValue(track.trackNumber);
    query.addBindValue(track.discNumber);
    query.addBindValue(track.durationMs);
    query.addBindValue(track.sampleRate);
    query.addBindValue(track.bitDepth);
    query.addBindValue(track.channels);
    query.addBindValue(track.codec);
    query.addBindValue(track.hasTrackGain ? 1 : 0);
    query.addBindValue(track.hasAlbumGain ? 1 : 0);
    query.addBindValue(track.trackGainDb);
    query.addBindValue(track.albumGainDb);
    query.addBindValue(track.trackPeak);
    query.addBindValue(track.albumPeak);
    query.addBindValue(track.lyrics);
    query.addBindValue(track.artworkHash);
    query.addBindValue(track.mtime);
    query.addBindValue(track.fileSize);
    query.addBindValue(track.cuePath);
    query.addBindValue(track.cueNumber);
    if (!exists) {
        query.addBindValue(track.cueStartMs);
    }
    query.addBindValue(track.cueEndMs >= 0 ? QVariant(track.cueEndMs) : QVariant());
    if (exists) {
        query.addBindValue(existingId);
    } else {
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
    }
    if (!query.exec()) {
        return 0;
    }
    return exists ? existingId : query.lastInsertId().toLongLong();
}

void Database::deleteStaleCueRows(qint64 rootId)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tracks WHERE root_id=? AND missing=1 AND cue_start_ms>=0 AND path IN "
                                 "(SELECT path FROM tracks WHERE root_id=? AND missing=0)"));
    query.addBindValue(rootId);
    query.addBindValue(rootId);
    query.exec();
}

void Database::removeTracks(const QVector<qint64>& ids)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    m_db.transaction();
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE track_id=?"));
    QSqlQuery tracks(m_db);
    tracks.prepare(QStringLiteral("DELETE FROM tracks WHERE id=?"));
    for (const qint64 id : ids) {
        query.bindValue(0, id);
        query.exec();
        tracks.bindValue(0, id);
        tracks.exec();
    }
    m_db.commit();
}

Track Database::trackFromQuery(const QSqlQuery& query) const
{
    Track track;
    track.id = query.value(0).toLongLong();
    track.rootId = query.value(1).toLongLong();
    track.path = query.value(2).toString();
    track.folder = query.value(3).toString();
    track.title = query.value(4).toString();
    track.artist = query.value(5).toString();
    track.albumArtist = query.value(6).toString();
    track.album = query.value(7).toString();
    track.albumKey = query.value(8).toString();
    track.artistKey = query.value(9).toString();
    track.year = query.value(10).toInt();
    track.trackNumber = query.value(11).toInt();
    track.discNumber = query.value(12).toInt();
    track.durationMs = query.value(13).toLongLong();
    track.sampleRate = query.value(14).toInt();
    track.bitDepth = query.value(15).toInt();
    track.channels = query.value(16).toInt();
    track.codec = query.value(17).toString();
    track.hasTrackGain = query.value(18).toInt() != 0;
    track.hasAlbumGain = query.value(19).toInt() != 0;
    track.trackGainDb = query.value(20).toDouble();
    track.albumGainDb = query.value(21).toDouble();
    track.trackPeak = query.value(22).toDouble();
    track.albumPeak = query.value(23).toDouble();
    track.lyrics = query.value(24).toString();
    track.artworkHash = query.value(25).toString();
    track.mtime = query.value(26).toLongLong();
    track.fileSize = query.value(27).toLongLong();
    track.missing = query.value(28).toInt() != 0;
    track.cuePath = query.value(29).toString();
    track.cueNumber = query.value(30).toInt();
    track.cueStartMs = query.value(31).toLongLong();
    track.cueEndMs = query.value(32).isNull() ? -1 : query.value(32).toLongLong();
    return track;
}

Track Database::track(qint64 id) const
{
    const auto rows = tracksByIds({id});
    return rows.isEmpty() ? Track {} : rows.first();
}

QVector<Track> Database::tracksByIds(const QVector<qint64>& ids) const
{
    QVector<Track> result;
    result.reserve(ids.size());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, root_id, path, folder, title, artist, album_artist, album, album_key, artist_key, "
                                 "year, track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, "
                                 "has_track_gain, has_album_gain, track_gain_db, album_gain_db, track_peak, album_peak, lyrics, "
                                 "artwork_hash, mtime, file_size, missing, cue_path, cue_number, cue_start_ms, cue_end_ms "
                                 "FROM tracks WHERE id=?"));
    for (const qint64 id : ids) {
        query.bindValue(0, id);
        if (query.exec() && query.next()) {
            result.push_back(trackFromQuery(query));
        }
    }
    return result;
}

QVector<Track> Database::queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const
{
    QVector<Track> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, root_id, path, folder, title, artist, album_artist, album, album_key, artist_key, "
                                 "year, track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, "
                                 "has_track_gain, has_album_gain, track_gain_db, album_gain_db, track_peak, album_peak, lyrics, "
                                 "artwork_hash, mtime, file_size, missing, cue_path, cue_number, cue_start_ms, cue_end_ms "
                                 "FROM tracks ")
                  + whereSql + QLatin1Char(' ') + orderSql);
    for (const QVariant& bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(trackFromQuery(query));
    }
    return result;
}

QVector<Album> Database::albums(const QString& artistKey) const
{
    QVector<Album> result;
    QSqlQuery query(m_db);
    QString sql = QStringLiteral(
        "SELECT album_key, MAX(album), MAX(album_artist), MAX(artist), MAX(year), COUNT(*), "
        "(SELECT artwork_hash FROM tracks t2 WHERE t2.album_key=tracks.album_key AND artwork_hash!='' LIMIT 1) "
        "FROM tracks WHERE missing=0 AND album_key!='' ");
    if (!artistKey.isEmpty()) {
        sql += QStringLiteral("AND artist_key=? ");
    }
    sql += QStringLiteral("GROUP BY album_key ORDER BY MAX(album) COLLATE NOCASE");
    query.prepare(sql);
    if (!artistKey.isEmpty()) {
        query.addBindValue(artistKey);
    }
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        Album album;
        album.key = query.value(0).toString();
        album.title = query.value(1).toString();
        const QString albumArtist = query.value(2).toString();
        album.artist = albumArtist.isEmpty() ? query.value(3).toString() : albumArtist;
        album.year = query.value(4).toInt();
        album.tracks = query.value(5).toInt();
        album.artworkHash = query.value(6).toString();
        if (album.title.isEmpty()) {
            album.title = QStringLiteral("未知专辑");
        }
        if (album.artist.isEmpty()) {
            album.artist = QStringLiteral("未知艺人");
        }
        result.push_back(album);
    }
    return result;
}

QVector<Artist> Database::artists() const
{
    QVector<Artist> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT artist_key, MAX(artist), COUNT(DISTINCT album_key), COUNT(*) FROM tracks "
                              "WHERE missing=0 GROUP BY artist_key ORDER BY MAX(artist) COLLATE NOCASE"));
    while (query.next()) {
        Artist artist;
        artist.key = query.value(0).toString();
        artist.name = query.value(1).toString();
        if (artist.name.trimmed().isEmpty()) {
            artist.name = QStringLiteral("未知艺人");
        }
        artist.albums = query.value(2).toInt();
        artist.tracks = query.value(3).toInt();
        result.push_back(artist);
    }
    return result;
}

QVector<QString> Database::folders() const
{
    QVector<QString> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT DISTINCT folder FROM tracks WHERE missing=0 AND folder!='' ORDER BY folder"));
    while (query.next()) {
        result.push_back(query.value(0).toString());
    }
    return result;
}

QVector<Track> Database::searchTracks(const QString& ftsQuery, int limit) const
{
    if (!m_fts || ftsQuery.isEmpty()) {
        return {};
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT t.id FROM tracks_fts f JOIN tracks t ON t.id=f.rowid WHERE tracks_fts MATCH ? LIMIT ?"));
    query.addBindValue(ftsQuery);
    query.addBindValue(limit);
    if (!query.exec()) {
        return {};
    }
    QVector<qint64> ids;
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    return tracksByIds(ids);
}

qint64 Database::favoritesPlaylistId() const
{
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id FROM playlists WHERE system_key='favorites'"));
    return query.next() ? query.value(0).toLongLong() : 0;
}

bool Database::setFavorites(const QVector<qint64>& ids, bool liked, QString* error)
{
    QMutexLocker locker(&writeLock());
    auto items = playlistItems(favoritesPlaylistId());
    QVector<qint64> added;
    for (qint64 id : ids) {
        if (liked) {
            if (!items.contains(id) && !added.contains(id) && track(id).id) added.push_back(id);
        } else {
            items.removeAll(id);
        }
    }
    return writeFavorites(added + items, error);
}

bool Database::restoreFavorites(const QVector<qint64>& ids, QString* error)
{
    QMutexLocker locker(&writeLock());
    return writeFavorites(ids, error);
}

bool Database::writeFavorites(const QVector<qint64>& ids, QString* error)
{
    const qint64 playlistId = favoritesPlaylistId();
    if (!playlistId || !m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery query(m_db);
    auto fail = [&] {
        if (error) *error = query.lastError().text();
        m_db.rollback();
        return false;
    };
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    query.addBindValue(playlistId);
    if (!query.exec()) return fail();
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, track_id, sort_order) SELECT ?,id,? FROM tracks WHERE id=?"));
    int index = 0;
    for (qint64 id : ids) {
        query.bindValue(0, playlistId);
        query.bindValue(1, index++);
        query.bindValue(2, id);
        if (!query.exec()) return fail();
    }
    if (!m_db.commit()) return fail();
    return true;
}

QVector<Playlist> Database::playlists() const
{
    QVector<Playlist> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id, name, folder_id, sort_order, system_key FROM playlists ORDER BY system_key IS NULL, sort_order, name"));
    while (query.next()) {
        result.push_back(Playlist {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
                                   query.value(3).toInt(), query.value(4).toString()});
    }
    return result;
}

QVector<PlaylistFolder> Database::playlistFolders() const
{
    QVector<PlaylistFolder> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id, name, parent_id, sort_order FROM playlist_folders ORDER BY sort_order, name"));
    while (query.next()) {
        result.push_back(PlaylistFolder {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
                                          query.value(3).toInt()});
    }
    return result;
}

qint64 Database::createPlaylist(const QString& name, qint64 folderId)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO playlists(name, folder_id, sort_order) VALUES (?,?, (SELECT COALESCE(MAX(sort_order),0)+1 FROM playlists))"));
    query.addBindValue(name);
    query.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant());
    query.exec();
    return query.lastInsertId().toLongLong();
}

qint64 Database::createPlaylistFolder(const QString& name, qint64 parentId)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO playlist_folders(name, parent_id, sort_order) VALUES (?,?, (SELECT COALESCE(MAX(sort_order),0)+1 FROM playlist_folders))"));
    query.addBindValue(name);
    query.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant());
    query.exec();
    return query.lastInsertId().toLongLong();
}

void Database::renamePlaylist(qint64 id, const QString& name)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    query.addBindValue(name);
    query.addBindValue(id);
    query.exec();
}

void Database::renamePlaylistFolder(qint64 id, const QString& name)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE playlist_folders SET name=? WHERE id=?"));
    query.addBindValue(name);
    query.addBindValue(id);
    query.exec();
}

void Database::deletePlaylist(qint64 id)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    query.addBindValue(id);
    query.exec();
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    query.addBindValue(id);
    query.exec();
}

void Database::deletePlaylistFolder(qint64 id)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE playlists SET folder_id=NULL WHERE folder_id=?"));
    query.addBindValue(id);
    query.exec();
    query.prepare(QStringLiteral("UPDATE playlist_folders SET parent_id=NULL WHERE parent_id=?"));
    query.addBindValue(id);
    query.exec();
    query.prepare(QStringLiteral("DELETE FROM playlist_folders WHERE id=?"));
    query.addBindValue(id);
    query.exec();
}

void Database::movePlaylist(qint64 id, qint64 folderId)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE playlists SET folder_id=? WHERE id=?"));
    query.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant());
    query.addBindValue(id);
    query.exec();
}

QVector<qint64> Database::playlistItems(qint64 playlistId) const
{
    QVector<qint64> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order, id"));
    query.addBindValue(playlistId);
    query.exec();
    while (query.next()) {
        result.push_back(query.value(0).toLongLong());
    }
    return result;
}

void Database::setPlaylistItems(qint64 playlistId, const QVector<qint64>& trackIds)
{
    if (playlistId == favoritesPlaylistId()) return;
    QMutexLocker locker(&writeLock());
    m_db.transaction();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    query.addBindValue(playlistId);
    query.exec();
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, track_id, sort_order) VALUES (?,?,?)"));
    for (int index = 0; index < trackIds.size(); ++index) {
        query.bindValue(0, playlistId);
        query.bindValue(1, trackIds[index]);
        query.bindValue(2, index);
        query.exec();
    }
    m_db.commit();
}

void Database::insertPlaylistItems(qint64 playlistId, int index, const QVector<qint64>& trackIds)
{
    if (playlistId == favoritesPlaylistId()) return;
    auto items = playlistItems(playlistId);
    index = std::clamp(index, 0, static_cast<int>(items.size()));
    for (int offset = 0; offset < trackIds.size(); ++offset) {
        items.insert(index + offset, trackIds[offset]);
    }
    setPlaylistItems(playlistId, items);
}

void Database::removePlaylistItems(qint64 playlistId, const QVector<int>& positions)
{
    if (playlistId == favoritesPlaylistId()) return;
    auto items = playlistItems(playlistId);
    QVector<int> sorted = positions;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (const int position : sorted) {
        if (position >= 0 && position < items.size()) {
            items.removeAt(position);
        }
    }
    setPlaylistItems(playlistId, items);
}

QString Database::setting(const QString& key, const QString& fallback) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"));
    query.addBindValue(key);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return fallback;
}

void Database::setSetting(const QString& key, const QString& value)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO settings(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    query.exec();
}

} // namespace echo
