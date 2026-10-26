#include "library/playlistrepository.h"

#include "library/metadata.h"

#include <QSqlError>
#include <QSqlQuery>

namespace echo {

using PlaylistEntry = Database::PlaylistEntry;

qint64 PlaylistRepository::favoritesPlaylistId() const
{
    QSqlQuery query(database());
    query.exec(QStringLiteral("SELECT id FROM playlists WHERE system_key='favorites'"));
    return query.next() ? query.value(0).toLongLong() : 0;
}

bool PlaylistRepository::setFavorites(const QVector<qint64>& ids, bool liked, QString* error)
{
    QMutexLocker locker(&Database::writeLock());
    auto items = playlistItems(favoritesPlaylistId());
    QVector<qint64> added;
    for (qint64 id : ids) {
        if (liked) {
            if (!items.contains(id) && !added.contains(id) && m_database.track(id).id) added.push_back(id);
        } else {
            items.removeAll(id);
        }
    }
    return writeFavorites(added + items, error);
}

bool PlaylistRepository::restoreFavorites(const QVector<qint64>& ids, QString* error)
{
    QMutexLocker locker(&Database::writeLock());
    return writeFavorites(ids, error);
}

bool PlaylistRepository::writeFavorites(const QVector<qint64>& ids, QString* error)
{
    const qint64 playlistId = favoritesPlaylistId();
    if (!playlistId || !database().transaction()) {
        if (error) *error = database().lastError().text();
        return false;
    }
    QSqlQuery query(database());
    auto fail = [&] {
        if (error) *error = query.lastError().text();
        database().rollback();
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
    if (!database().commit()) return fail();
    return true;
}

QVector<Playlist> PlaylistRepository::playlists() const
{
    QVector<Playlist> result;
    QSqlQuery query(database());
    query.exec(QStringLiteral("SELECT id, name, folder_id, sort_order, system_key FROM playlists ORDER BY system_key IS NULL, sort_order, name"));
    while (query.next()) {
        result.push_back(Playlist {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
                                   query.value(3).toInt(), query.value(4).toString()});
    }
    return result;
}

QVector<PlaylistFolder> PlaylistRepository::playlistFolders() const
{
    QVector<PlaylistFolder> result;
    QSqlQuery query(database());
    query.exec(QStringLiteral("SELECT id, name, parent_id, sort_order FROM playlist_folders ORDER BY sort_order, name"));
    while (query.next()) {
        result.push_back(PlaylistFolder {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
                                          query.value(3).toInt()});
    }
    return result;
}

qint64 PlaylistRepository::createPlaylist(const QString& name, qint64 folderId)
{
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("INSERT INTO playlists(name, folder_id, sort_order) VALUES (?,?, (SELECT COALESCE(MAX(sort_order),0)+1 FROM playlists))"));
    query.addBindValue(name);
    query.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant());
    query.exec();
    return query.lastInsertId().toLongLong();
}

qint64 PlaylistRepository::createPlaylistFolder(const QString& name, qint64 parentId)
{
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("INSERT INTO playlist_folders(name, parent_id, sort_order) VALUES (?,?, (SELECT COALESCE(MAX(sort_order),0)+1 FROM playlist_folders))"));
    query.addBindValue(name);
    query.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant());
    query.exec();
    return query.lastInsertId().toLongLong();
}

void PlaylistRepository::renamePlaylist(qint64 id, const QString& name)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    query.addBindValue(name);
    query.addBindValue(id);
    query.exec();
}

void PlaylistRepository::renamePlaylistFolder(qint64 id, const QString& name)
{
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("UPDATE playlist_folders SET name=? WHERE id=?"));
    query.addBindValue(name);
    query.addBindValue(id);
    query.exec();
}

void PlaylistRepository::deletePlaylist(qint64 id)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    query.addBindValue(id);
    query.exec();
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    query.addBindValue(id);
    query.exec();
}

void PlaylistRepository::deletePlaylistFolder(qint64 id)
{
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
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

void PlaylistRepository::movePlaylist(qint64 id, qint64 folderId)
{
    if (id == favoritesPlaylistId()) return;
    QMutexLocker locker(&Database::writeLock());
    QSqlQuery query(database());
    query.prepare(QStringLiteral("UPDATE playlists SET folder_id=? WHERE id=?"));
    query.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant());
    query.addBindValue(id);
    query.exec();
}

QVector<Database::PlaylistEntry> PlaylistRepository::playlistEntries(qint64 playlistId) const
{
    QVector<PlaylistEntry> result;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT id, track_id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order, id"));
    query.addBindValue(playlistId);
    query.exec();
    while (query.next())
        result.push_back({ query.value(0).toLongLong(), query.value(1).toLongLong() });
    return result;
}

QVector<qint64> PlaylistRepository::playlistItems(qint64 playlistId) const
{
    QVector<qint64> result;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order, id"));
    query.addBindValue(playlistId);
    query.exec();
    while (query.next()) {
        result.push_back(query.value(0).toLongLong());
    }
    return result;
}

void PlaylistRepository::setPlaylistItems(qint64 playlistId, const QVector<qint64>& trackIds)
{
    if (playlistId == favoritesPlaylistId()) return;
    QMutexLocker locker(&Database::writeLock());
    database().transaction();
    QSqlQuery query(database());
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
    database().commit();
}

void PlaylistRepository::insertPlaylistItems(qint64 playlistId, int index, const QVector<qint64>& trackIds)
{
    if (playlistId == favoritesPlaylistId() || trackIds.isEmpty())
        return;
    QMutexLocker locker(&Database::writeLock());
    if (!database().transaction())
        return;
    QSqlQuery query(database());
    query.prepare(
        QStringLiteral("UPDATE playlist_items SET sort_order=sort_order+? WHERE playlist_id=? AND sort_order>=?"));
    query.addBindValue(trackIds.size());
    query.addBindValue(playlistId);
    query.addBindValue(index);
    if (!query.exec()) {
        database().rollback();
        return;
    }
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id,track_id,sort_order) VALUES (?,?,?)"));
    for (int i = 0; i < trackIds.size(); ++i) {
        query.bindValue(0, playlistId);
        query.bindValue(1, trackIds[i]);
        query.bindValue(2, index + i);
        if (!query.exec()) {
            database().rollback();
            return;
        }
    }
    database().commit();
}

void PlaylistRepository::removePlaylistItems(qint64 playlistId, const QVector<int>& positions)
{
    if (playlistId == favoritesPlaylistId())
        return;
    QMutexLocker locker(&Database::writeLock());
    if (!database().transaction())
        return;
    QSqlQuery query(database());
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
                database().rollback();
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
            database().rollback();
            return;
        }
    }
    database().commit();
}

void PlaylistRepository::reorderPlaylist(qint64 id, int from, int to)
{
    if (id == favoritesPlaylistId())
        return;
    QMutexLocker locker(&Database::writeLock());
    if (!database().transaction())
        return;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT id FROM playlist_items WHERE playlist_id=? ORDER BY sort_order,id"));
    query.addBindValue(id);
    query.exec();
    QVector<qint64> entries;
    while (query.next())
        entries << query.value(0).toLongLong();
    if (from < 0 || to < 0 || from >= entries.size() || to >= entries.size()) {
        database().rollback();
        return;
    }
    entries.move(from, to);
    query.prepare(QStringLiteral("UPDATE playlist_items SET sort_order=? WHERE id=?"));
    for (int i = 0; i < entries.size(); ++i) {
        query.bindValue(0, i);
        query.bindValue(1, entries[i]);
        if (!query.exec()) {
            database().rollback();
            return;
        }
    }
    database().commit();
}

qint64 PlaylistRepository::createPlaylistWithItems(const QString& name, const QVector<qint64>& ids)
{
    QMutexLocker locker(&Database::writeLock());
    if (!database().transaction())
        return 0;
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO playlists(name,sort_order) VALUES (?,(SELECT COALESCE(MAX(sort_order),0)+1 FROM playlists))"));
    query.addBindValue(name);
    if (!query.exec()) {
        database().rollback();
        return 0;
    }
    const qint64 id = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id,track_id,sort_order) VALUES (?,?,?)"));
    for (int i = 0; i < ids.size(); ++i) {
        query.bindValue(0, id);
        query.bindValue(1, ids[i]);
        query.bindValue(2, i);
        if (!query.exec()) {
            database().rollback();
            return 0;
        }
    }
    if (!database().commit()) {
        database().rollback();
        return 0;
    }
    return id;
}

} // namespace echo
