#include "library/database.h"
#include "library/trackrepository.h"
#include "library/playlistrepository.h"
#include "library/schema.h"
#include "library/searchindex.h"

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
    return SchemaMigrator::migrate(m_db, readOnly, &m_fts, error);
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







QVector<Track> Database::searchTracks(const QString& ftsQuery, int limit) const
{
    return SearchIndex(*this).match(ftsQuery, limit);
}

QVector<Track> Database::searchTracksContaining(const QString& text, int limit) const
{
    return SearchIndex(*this).containing(text, limit);
}














QVector<Database::PlaylistEntry> Database::playlistEntries(qint64 playlistId) const
{
    return PlaylistRepository(*this).playlistEntries(playlistId);
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

Track Database::trackFromQuery(const QSqlQuery& query) const
{
    return TrackRepository(*this).trackFromQuery(query);
}

Track Database::track(qint64 id) const
{
    return TrackRepository(*this).track(id);
}

QVector<Track> Database::tracksByIds(const QVector<qint64>& ids) const
{
    return TrackRepository(*this).tracksByIds(ids);
}

QVector<Track> Database::queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const
{
    return TrackRepository(*this).queryTracks(whereSql, binds, orderSql);
}

QVector<Album> Database::albums(const QString& artistKey) const
{
    return TrackRepository(*this).albums(artistKey);
}

QVector<Artist> Database::artists() const
{
    return TrackRepository(*this).artists();
}

qint64 Database::favoritesPlaylistId() const
{
    return PlaylistRepository(*this).favoritesPlaylistId();
}

bool Database::setFavorites(const QVector<qint64>& ids, bool liked, QString* error)
{
    return PlaylistRepository(*this).setFavorites(ids, liked, error);
}

bool Database::restoreFavorites(const QVector<qint64>& ids, QString* error)
{
    return PlaylistRepository(*this).restoreFavorites(ids, error);
}

bool Database::writeFavorites(const QVector<qint64>& ids, QString* error)
{
    return PlaylistRepository(*this).writeFavorites(ids, error);
}

QVector<Playlist> Database::playlists() const
{
    return PlaylistRepository(*this).playlists();
}

QVector<PlaylistFolder> Database::playlistFolders() const
{
    return PlaylistRepository(*this).playlistFolders();
}

qint64 Database::createPlaylist(const QString& name, qint64 folderId)
{
    return PlaylistRepository(*this).createPlaylist(name, folderId);
}

qint64 Database::createPlaylistFolder(const QString& name, qint64 parentId)
{
    return PlaylistRepository(*this).createPlaylistFolder(name, parentId);
}

void Database::renamePlaylist(qint64 id, const QString& name)
{
    PlaylistRepository(*this).renamePlaylist(id, name);
}

void Database::renamePlaylistFolder(qint64 id, const QString& name)
{
    PlaylistRepository(*this).renamePlaylistFolder(id, name);
}

void Database::deletePlaylist(qint64 id)
{
    PlaylistRepository(*this).deletePlaylist(id);
}

void Database::deletePlaylistFolder(qint64 id)
{
    PlaylistRepository(*this).deletePlaylistFolder(id);
}

void Database::movePlaylist(qint64 id, qint64 folderId)
{
    PlaylistRepository(*this).movePlaylist(id, folderId);
}

QVector<qint64> Database::playlistItems(qint64 playlistId) const
{
    return PlaylistRepository(*this).playlistItems(playlistId);
}

void Database::setPlaylistItems(qint64 playlistId, const QVector<qint64>& trackIds)
{
    PlaylistRepository(*this).setPlaylistItems(playlistId, trackIds);
}

void Database::insertPlaylistItems(qint64 playlistId, int index, const QVector<qint64>& trackIds)
{
    PlaylistRepository(*this).insertPlaylistItems(playlistId, index, trackIds);
}

void Database::removePlaylistItems(qint64 playlistId, const QVector<int>& positions)
{
    PlaylistRepository(*this).removePlaylistItems(playlistId, positions);
}

void Database::reorderPlaylist(qint64 id, int from, int to)
{
    PlaylistRepository(*this).reorderPlaylist(id, from, to);
}

qint64 Database::createPlaylistWithItems(const QString& name, const QVector<qint64>& ids)
{
    return PlaylistRepository(*this).createPlaylistWithItems(name, ids);
}

} // namespace echo

