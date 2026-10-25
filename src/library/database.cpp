#include "library/database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStorageInfo>
#include <QSqlError>
#include <QVariant>

#include <algorithm>

namespace echo {
namespace {

    const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS roots (
  id INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE,
  name TEXT, active INTEGER NOT NULL DEFAULT 1, revision INTEGER NOT NULL DEFAULT 1,
  status TEXT NOT NULL DEFAULT 'available', message TEXT, mount_path TEXT, checked_at INTEGER NOT NULL DEFAULT 0
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

bool Database::open(const QString& filePath, const QString& connectionName, QString* error, bool readOnly)
{
#ifdef ECHO_QT_PLUGINS
    if (!QDir(QCoreApplication::applicationDirPath()).exists(QStringLiteral("../PlugIns/sqldrivers"))) {
        QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
    }
#endif
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    }
    m_db.setConnectOptions(readOnly ? QStringLiteral("QSQLITE_OPEN_READONLY") : QString());
    m_db.setDatabaseName(filePath);
    if (!m_db.open()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }
    m_path = filePath;
    QSqlQuery query(m_db);
    if (readOnly) {
        query.exec(QStringLiteral("PRAGMA busy_timeout=1000"));
        m_fts = query.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name='tracks_fts'")) && query.next();
        return true;
    }
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
    for (const auto& column : QStringList { "name TEXT", "active INTEGER NOT NULL DEFAULT 1",
             "revision INTEGER NOT NULL DEFAULT 1", "status TEXT NOT NULL DEFAULT 'available'", "message TEXT",
             "mount_path TEXT", "checked_at INTEGER NOT NULL DEFAULT 0" }) {
        const auto name = column.section(u' ', 0, 0);
        QSqlQuery info(m_db);
        info.exec(QStringLiteral("PRAGMA table_info(roots)"));
        bool found = false;
        while (info.next())
            found |= info.value(1).toString() == name;
        if (!found && !query.exec(QStringLiteral("ALTER TABLE roots ADD COLUMN ") + column)) {
            if (error)
                *error = query.lastError().text();
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
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO roots(path,name,mount_path) VALUES (?,?,?)"));
    query.addBindValue(path);
    query.addBindValue(QFileInfo(QDir::cleanPath(path)).fileName());
    query.addBindValue(QStorageInfo(path).rootPath());
    query.exec();
    query.prepare(QStringLiteral("UPDATE roots SET active=1,revision=revision+1 WHERE path=? AND active=0"));
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
    if (!m_db.transaction())
        return;
    query.prepare(QStringLiteral("UPDATE tracks SET missing=1 WHERE root_id=? AND EXISTS (SELECT 1 FROM roots WHERE "
                                 "id=? AND status!='available')"));
    query.addBindValue(id);
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.rollback();
        return;
    }
    query.prepare(QStringLiteral("UPDATE roots SET active=0, revision=revision+1 WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.rollback();
        return;
    }
    m_db.commit();
}

LibraryRoot Database::root(qint64 id) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id,path,name,status,message,revision,checked_at,active,mount_path,(SELECT COUNT(*) FROM tracks WHERE "
        "root_id=roots.id),(SELECT COUNT(*) FROM tracks WHERE root_id=roots.id AND missing=0) FROM roots WHERE id=?"));
    query.addBindValue(id);
    query.exec();
    if (!query.next())
        return { };
    LibraryRoot root;
    root.id = query.value(0).toLongLong();
    root.path = query.value(1).toString();
    root.name = query.value(2).toString();
    if (root.name.isEmpty() || root.name == root.path)
        root.name = QFileInfo(QDir::cleanPath(root.path)).fileName();
    if (root.name.isEmpty())
        root.name = root.path;
    root.status = query.value(3).toString();
    root.message = query.value(4).toString();
    root.revision = query.value(5).toLongLong();
    root.checkedAt = query.value(6).toLongLong();
    root.active = query.value(7).toBool();
    root.mountPath = query.value(8).toString();
    root.tracks = query.value(9).toInt();
    root.availableTracks = root.status == QLatin1String("available") ? query.value(10).toInt() : 0;
    return root;
}

QVector<LibraryRoot> Database::roots() const
{
    QVector<LibraryRoot> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id FROM roots WHERE active=1 ORDER BY id"));
    while (query.next())
        result.push_back(root(query.value(0).toLongLong()));
    return result;
}

QString Database::libraryScope()
{
    return QStringLiteral("EXISTS (SELECT 1 FROM roots r WHERE r.id=tracks.root_id AND r.active=1)");
}

void Database::setRootStatus(
    qint64 id, const QString& path, qint64 revision, const QString& status, const QString& message)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE roots SET status=?,message=?,checked_at=? WHERE id=? AND path=? AND revision=? AND active=1"));
    for (const QVariant& value :
        QVariantList { status, message, QDateTime::currentSecsSinceEpoch(), id, path, revision })
        query.addBindValue(value);
    query.exec();
}

bool Database::commitScan(qint64 id, const QString& path, qint64 revision, const QVector<Track>& tracks,
    const QString& signature, QString* error)
{
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return false;
    const auto current = root(id);
    if (!current.active || current.path != path || current.revision != revision) {
        m_db.rollback();
        return true;
    }
    QSqlQuery query(m_db);
    auto fail = [&] {
        if (error)
            *error = query.lastError().text();
        m_db.rollback();
        return false;
    };
    const auto previous = queryTracks(QStringLiteral("WHERE root_id=?"), { id }, { });
    auto key = [](const Track& track) { return track.path + u'\n' + QString::number(track.cueStartMs); };
    QHash<QString, Track> known;
    QSet<QString> present;
    for (const auto& old : previous)
        known.insert(key(old), old);
    for (auto track : tracks) {
        const auto identity = key(track);
        present.insert(identity);
        const auto old = known.value(identity);
        if (old.id)
            track.id = old.id;
        if (!old.id || track != old) {
            if (!upsertTrackUnlocked(track)) {
                if (error)
                    *error = QStringLiteral("写入扫描结果失败");
                m_db.rollback();
                return false;
            }
        }
    }
    query.prepare(QStringLiteral("UPDATE tracks SET missing=1 WHERE id=? AND missing=0"));
    for (const auto& old : previous)
        if (!present.contains(key(old))) {
            query.bindValue(0, old.id);
            if (!query.exec())
                return fail();
        }
    query.prepare(QStringLiteral("UPDATE roots SET status='available',message='',checked_at=? WHERE id=?"));
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(id);
    if (!query.exec())
        return fail();
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO settings(key,value) VALUES (?,?)"));
    query.addBindValue(QStringLiteral("scan_signature_%1").arg(id));
    query.addBindValue(signature);
    if (!query.exec())
        return fail();
    if (!m_db.commit())
        return fail();
    return true;
}

bool Database::updateRoot(
    qint64 id, const QString& path, const QVector<QPair<qint64, QString>>& matches, QString* error)
{
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return false;
    const auto old = root(id);
    QSqlQuery query(m_db);
    auto fail = [&] {
        if (error)
            *error = query.lastError().text();
        m_db.rollback();
        return false;
    };
    query.prepare(QStringLiteral(
        "UPDATE roots SET path=?,name=?,mount_path=?,revision=revision+1,status='available',message='' WHERE id=?"));
    query.addBindValue(path);
    query.addBindValue(QFileInfo(QDir::cleanPath(path)).fileName());
    query.addBindValue(QStorageInfo(path).rootPath());
    query.addBindValue(id);
    if (!query.exec())
        return fail();
    query.prepare(QStringLiteral("UPDATE tracks SET missing=1 WHERE root_id=?"));
    query.addBindValue(id);
    if (!query.exec())
        return fail();
    query.prepare(
        QStringLiteral("UPDATE tracks SET path=?,folder=?,missing=0,cue_path=CASE WHEN cue_path=path THEN ? WHEN "
                       "substr(cue_path,1,?)=? THEN ?||substr(cue_path,?) ELSE cue_path END WHERE id=?"));
    for (const auto& match : matches) {
        query.bindValue(0, match.second);
        query.bindValue(1, path);
        query.bindValue(2, match.second);
        query.bindValue(3, old.path.size() + 1);
        query.bindValue(4, old.path + u'/');
        query.bindValue(5, path);
        query.bindValue(6, old.path.size() + 1);
        query.bindValue(7, match.first);
        if (!query.exec())
            return fail();
    }
    if (!m_db.commit())
        return fail();
    return true;
}

bool Database::relocateTrack(qint64 id, const QString& path, QString* error)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tracks SET path=?,folder=?,root_id=(SELECT id FROM roots WHERE path=? AND "
                                 "active=1),missing=0,cue_path=CASE WHEN cue_path=path THEN ? ELSE cue_path END WHERE "
                                 "path=(SELECT path FROM tracks WHERE id=?)"));
    const auto folder = QFileInfo(path).absolutePath();
    query.addBindValue(path);
    query.addBindValue(folder);
    query.addBindValue(folder);
    query.addBindValue(path);
    query.addBindValue(id);
    if (query.exec())
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

bool Database::updateLooseAvailability(const QVector<Track>& tracks)
{
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tracks SET missing=? WHERE id=? AND path=? AND mtime=? AND missing!=? AND NOT "
                                 "EXISTS (SELECT 1 FROM roots r WHERE r.id=tracks.root_id AND r.active=1)"));
    bool changed = false;
    for (const auto& track : tracks) {
        query.bindValue(0, track.missing ? 1 : 0);
        query.bindValue(1, track.id);
        query.bindValue(2, track.path);
        query.bindValue(3, track.mtime);
        query.bindValue(4, track.missing ? 1 : 0);
        if (!query.exec()) {
            m_db.rollback();
            return false;
        }
        changed |= query.numRowsAffected() > 0;
    }
    if (!m_db.commit())
        return false;
    return changed;
}

void Database::markPathMissing(const QString& path)
{
    QMutexLocker locker(&writeLock());
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tracks SET missing=1 WHERE path=?"));
    query.addBindValue(path);
    query.exec();
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
    return upsertTrackUnlocked(track);
}

qint64 Database::upsertTrackUnlocked(const Track& track)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM tracks WHERE path=? AND cue_start_ms=?"));
    query.addBindValue(track.path);
    query.addBindValue(track.cueStartMs);
    query.exec();
    const bool exists = query.next();
    const qint64 existingId = exists ? query.value(0).toLongLong() : 0;
    if (exists) {
        query.prepare(QStringLiteral(
            "UPDATE tracks SET root_id=COALESCE(?,root_id), folder=?, title=?, artist=?, album_artist=?, album=?, "
            "album_key=?, artist_key=?, "
            "year=?, track_number=?, disc_number=?, duration_ms=?, sample_rate=?, bit_depth=?, channels=?, codec=?, "
            "has_track_gain=?, has_album_gain=?, track_gain_db=?, album_gain_db=?, track_peak=?, album_peak=?, "
            "lyrics=?, "
            "artwork_hash=?, mtime=?, file_size=?, missing=?, cue_path=?, cue_number=?, cue_end_ms=? WHERE id=?"));
    } else {
        query.prepare(QStringLiteral("INSERT INTO tracks(root_id, path, folder, title, artist, album_artist, album, "
                                     "album_key, artist_key, year, "
                                     "track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, "
                                     "has_track_gain, has_album_gain, "
                                     "track_gain_db, album_gain_db, track_peak, album_peak, lyrics, artwork_hash, "
                                     "mtime, file_size, missing, cue_path, "
                                     "cue_number, cue_start_ms, cue_end_ms, added_at) VALUES "
                                     "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
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
    query.addBindValue(track.missing ? 1 : 0);
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
    QHash<qint64, Track> found;
    // Bounded IN batches avoid one SQL query per row while respecting SQLite bind limits.
    for (int offset = 0; offset < ids.size(); offset += 400) {
        QVariantList binds;
        QStringList placeholders;
        for (int i = offset; i < std::min(offset + 400, static_cast<int>(ids.size())); ++i) {
            binds << ids[i];
            placeholders << QStringLiteral("?");
        }
        for (const auto& track :
            queryTracks(QStringLiteral("WHERE id IN (") + placeholders.join(u',') + u')', binds, { }))
            found.insert(track.id, track);
    }
    result.reserve(ids.size());
    for (const auto id : ids)
        result.push_back(found.value(id));
    return result;
}

QVector<Track> Database::queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const
{
    QVector<Track> result;
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT id, root_id, path, folder, title, artist, album_artist, album, album_key, artist_key, "
                       "year, track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, "
                       "has_track_gain, has_album_gain, track_gain_db, album_gain_db, track_peak, album_peak, lyrics, "
                       "artwork_hash, mtime, file_size, CASE WHEN EXISTS (SELECT 1 FROM roots r WHERE "
                       "r.id=tracks.root_id AND r.active=1 AND r.status!='available') THEN 1 ELSE missing END, "
                       "cue_path, cue_number, cue_start_ms, cue_end_ms "
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
        "FROM tracks WHERE %1 AND album_key!='' ")
                      .arg(libraryScope());
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
            album.artist = QStringLiteral("未知艺术家");
        }
        result.push_back(album);
    }
    return result;
}

QVector<Artist> Database::artists() const
{
    QVector<Artist> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT artist_key, MAX(artist), COUNT(DISTINCT album_key), COUNT(*) FROM tracks "
                                 "WHERE %1 GROUP BY artist_key "
                                 "ORDER BY CASE WHEN artist_key=? THEN 1 ELSE 0 END, MAX(artist) COLLATE NOCASE")
                      .arg(libraryScope()));
    query.addBindValue(makeArtistKey({}));
    query.exec();
    while (query.next()) {
        Artist artist;
        artist.key = query.value(0).toString();
        artist.name = query.value(1).toString();
        if (artist.name.trimmed().isEmpty()) {
            artist.name = QStringLiteral("未知艺术家");
        }
        artist.albums = query.value(2).toInt();
        artist.tracks = query.value(3).toInt();
        result.push_back(artist);
    }
    return result;
}

QVector<Track> Database::searchTracks(const QString& ftsQuery, int limit) const
{
    if (!m_fts || ftsQuery.isEmpty()) {
        return {};
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT t.id FROM tracks_fts f JOIN tracks t ON t.id=f.rowid JOIN roots r ON "
                                 "r.id=t.root_id AND r.active=1 WHERE tracks_fts MATCH ? LIMIT ?"));
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

QVector<Track> Database::searchTracksContaining(const QString& text, int limit) const
{
    QSqlQuery query(m_db);
    if (limit <= 0 || !query.exec(QStringLiteral("SELECT id,title,artist,album FROM tracks WHERE ")
            + libraryScope() + QStringLiteral(" ORDER BY title,id"))) return {};
    QVector<qint64> ids;
    // SQLite lower() only folds ASCII. Keep the original Unicode case-insensitive semantics,
    // streaming small metadata rows instead of loading every track (including lyrics).
    while (query.next()) {
        if (query.value(1).toString().contains(text, Qt::CaseInsensitive)
            || query.value(2).toString().contains(text, Qt::CaseInsensitive)
            || query.value(3).toString().contains(text, Qt::CaseInsensitive)) {
            ids << query.value(0).toLongLong();
            if (ids.size() >= limit) break;
        }
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
    if (playlistId == favoritesPlaylistId() || trackIds.isEmpty())
        return;
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return;
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("UPDATE playlist_items SET sort_order=sort_order+? WHERE playlist_id=? AND sort_order>=?"));
    query.addBindValue(trackIds.size());
    query.addBindValue(playlistId);
    query.addBindValue(index);
    if (!query.exec()) {
        m_db.rollback();
        return;
    }
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id,track_id,sort_order) VALUES (?,?,?)"));
    for (int i = 0; i < trackIds.size(); ++i) {
        query.bindValue(0, playlistId);
        query.bindValue(1, trackIds[i]);
        query.bindValue(2, index + i);
        if (!query.exec()) {
            m_db.rollback();
            return;
        }
    }
    m_db.commit();
}

void Database::removePlaylistItems(qint64 playlistId, const QVector<int>& positions)
{
    if (playlistId == favoritesPlaylistId())
        return;
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id"));
    query.addBindValue(playlistId);
    query.exec();
    QVector<qint64> entries;
    while (query.next())
        entries << query.value(0).toLongLong();
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE id=?"));
    for (const int at : positions)
        if (at >= 0 && at < entries.size()) {
            query.bindValue(0, entries[at]);
            if (!query.exec()) {
                m_db.rollback();
                return;
            }
        }
    query.prepare(QStringLiteral("SELECT id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id"));
    query.addBindValue(playlistId);
    query.exec();
    entries.clear();
    while (query.next())
        entries << query.value(0).toLongLong();
    query.prepare(QStringLiteral("UPDATE playlist_items SET sort_order=? WHERE id=?"));
    for (int i = 0; i < entries.size(); ++i) {
        query.bindValue(0, i);
        query.bindValue(1, entries[i]);
        if (!query.exec()) {
            m_db.rollback();
            return;
        }
    }
    m_db.commit();
}

void Database::reorderPlaylist(qint64 id, int from, int to)
{
    if (id == favoritesPlaylistId())
        return;
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id"));
    query.addBindValue(id);
    query.exec();
    QVector<qint64> entries;
    while (query.next())
        entries << query.value(0).toLongLong();
    if (from < 0 || to < 0 || from >= entries.size() || to >= entries.size()) {
        m_db.rollback();
        return;
    }
    entries.move(from, to);
    query.prepare(QStringLiteral("UPDATE playlist_items SET sort_order=? WHERE id=?"));
    for (int i = 0; i < entries.size(); ++i) {
        query.bindValue(0, i);
        query.bindValue(1, entries[i]);
        if (!query.exec()) {
            m_db.rollback();
            return;
        }
    }
    m_db.commit();
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

namespace echo {
qint64 Database::createPlaylistWithItems(const QString& name, const QVector<qint64>& ids)
{
    QMutexLocker locker(&writeLock());
    if (!m_db.transaction())
        return 0;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO playlists(name,sort_order) VALUES (?,(SELECT COALESCE(MAX(sort_order),0)+1 FROM playlists))"));
    query.addBindValue(name);
    if (!query.exec()) {
        m_db.rollback();
        return 0;
    }
    const qint64 id = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id,track_id,sort_order) VALUES (?,?,?)"));
    for (int i = 0; i < ids.size(); ++i) {
        query.bindValue(0, id);
        query.bindValue(1, ids[i]);
        query.bindValue(2, i);
        if (!query.exec()) {
            m_db.rollback();
            return 0;
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return 0;
    }
    return id;
}
}
