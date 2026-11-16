#pragma once

#include "library/database.h"

namespace echo {

class PlaylistRepository {
public:
    explicit PlaylistRepository(const Database& database) : m_database(database) {}
    [[nodiscard]] qint64 favoritesPlaylistId() const;
    bool setFavorites(const QVector<qint64>& ids, bool liked, QString* error);
    bool restoreFavorites(const QVector<qint64>& ids, QString* error);
    bool writeFavorites(const QVector<qint64>& ids, QString* error);
    [[nodiscard]] QVector<Playlist> playlists() const;
    [[nodiscard]] QVector<PlaylistFolder> playlistFolders() const;
    qint64 createPlaylist(const QString& name, qint64 folderId);
    qint64 createPlaylistFolder(const QString& name, qint64 parentId);
    void renamePlaylist(qint64 id, const QString& name);
    void renamePlaylistFolder(qint64 id, const QString& name);
    void deletePlaylist(qint64 id);
    void deletePlaylistFolder(qint64 id);
    void movePlaylist(qint64 id, qint64 folderId);
    [[nodiscard]] QVector<Database::PlaylistEntry> playlistEntries(qint64 playlistId) const;
    [[nodiscard]] QVector<qint64> playlistItems(qint64 playlistId) const;
    void setPlaylistItems(qint64 playlistId, const QVector<qint64>& trackIds);
    void insertPlaylistItems(qint64 playlistId, int index, const QVector<qint64>& trackIds);
    void removePlaylistItems(qint64 playlistId, const QVector<int>& positions);
    void reorderPlaylist(qint64 id, int from, int to);
    qint64 createPlaylistWithItems(const QString& name, const QVector<qint64>& ids);

private:
    [[nodiscard]] QSqlDatabase database() const { return m_database.connection(); }
    const Database& m_database;
};

} // namespace echo
