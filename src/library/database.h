#pragma once

#include "library/types.h"

#include <QMutex>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace echo {

class Database {
public:
    static QMutex& writeLock();

    bool open(const QString& filePath, const QString& connectionName, QString* error = nullptr);
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString filePath() const { return m_path; }

    qint64 addRoot(const QString& path);
    void removeRoot(qint64 id);
    [[nodiscard]] QVector<LibraryRoot> roots() const;

    [[nodiscard]] LibraryRoot root(qint64 id) const;
    bool updateRoot(qint64 id, const QString& path, const QVector<QPair<qint64, QString>>& matches, QString* error);
    void setRootStatus(qint64 id, const QString& path, qint64 revision, const QString& status, const QString& message);
    bool commitScan(qint64 id, const QString& path, qint64 revision, const QVector<Track>& tracks,
        const QString& signature, QString* error);
    [[nodiscard]] static QString libraryScope();
    bool relocateTrack(qint64 id, const QString& path, QString* error);
    bool updateLooseAvailability(const QVector<Track>& tracks);
    void markPathMissing(const QString& path);
    qint64 createPlaylistWithItems(const QString& name, const QVector<qint64>& ids);

    void markRootMissing(qint64 rootId);
    qint64 upsertTrack(const Track& track);
    void removeTracks(const QVector<qint64>& ids);

    [[nodiscard]] Track track(qint64 id) const;
    [[nodiscard]] QVector<Track> tracksByIds(const QVector<qint64>& ids) const;
    [[nodiscard]] QVector<Track> queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const;
    [[nodiscard]] QVector<Album> albums(const QString& artistKey) const;
    [[nodiscard]] QVector<Artist> artists() const;
    [[nodiscard]] QVector<Track> searchTracks(const QString& ftsQuery, int limit) const;
    [[nodiscard]] bool hasFts() const { return m_fts; }

    [[nodiscard]] qint64 favoritesPlaylistId() const;
    bool setFavorites(const QVector<qint64>& ids, bool liked, QString* error = nullptr);
    [[nodiscard]] QVector<Playlist> playlists() const;
    [[nodiscard]] QVector<PlaylistFolder> playlistFolders() const;
    qint64 createPlaylist(const QString& name, qint64 folderId);
    qint64 createPlaylistFolder(const QString& name, qint64 parentId);
    void renamePlaylist(qint64 id, const QString& name);
    void renamePlaylistFolder(qint64 id, const QString& name);
    void deletePlaylist(qint64 id);
    void deletePlaylistFolder(qint64 id);
    void reorderPlaylist(qint64 id, int from, int to);
    void movePlaylist(qint64 id, qint64 folderId);
    [[nodiscard]] QVector<qint64> playlistItems(qint64 playlistId) const;
    void setPlaylistItems(qint64 playlistId, const QVector<qint64>& trackIds);
    void insertPlaylistItems(qint64 playlistId, int index, const QVector<qint64>& trackIds);
    void removePlaylistItems(qint64 playlistId, const QVector<int>& positions);

    [[nodiscard]] QString setting(const QString& key, const QString& fallback = {}) const;
    void setSetting(const QString& key, const QString& value);

private:
    friend class Library;
    qint64 upsertTrackUnlocked(const Track& track);
    bool restoreFavorites(const QVector<qint64>& ids, QString* error);
    bool writeFavorites(const QVector<qint64>& ids, QString* error); // Caller holds writeLock.
    [[nodiscard]] Track trackFromQuery(const QSqlQuery& query) const;
    QSqlDatabase m_db;
    QString m_path;
    bool m_fts = false;
};

} // namespace echo
