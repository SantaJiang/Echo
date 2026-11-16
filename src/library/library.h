#pragma once

#include "library/artwork.h"
#include "library/database.h"
#include "library/types.h"
#include "playback/tracksource.h"

#include <QFileSystemWatcher>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QSet>
#include <functional>
#include <memory>

namespace echo {

class Scanner;
struct SearchResults {
    QVector<Track> tracks;
    QVector<Album> albums;
    QVector<Artist> artists;
    QVector<Playlist> playlists;
};

class Library : public TrackSource {
    Q_OBJECT

public:
    explicit Library(QObject* parent = nullptr);
    ~Library() override;

    bool open(QString* error = nullptr);
    [[nodiscard]] ArtworkCache* artwork() const { return m_artwork.get(); }
    [[nodiscard]] QString databasePath() const { return m_database ? m_database->filePath() : QString(); }

    qint64 addRoot(const QString& path);
    void refreshRoot(qint64 id);
    [[nodiscard]] LibraryRoot root(qint64 id) const;
    bool relocateRoot(qint64 id, const QString& path, QString* error);
    bool relocateTrack(qint64 id, const QString& path, QString* error);
    void recheckTrack(qint64 id);
    [[nodiscard]] QVector<Track> tracksAtPath(const QString& path) const;
    void markMissingPath(const QString& path) override;
    qint64 createPlaylistFromFolder(qint64 rootId, const QString& name, const QVector<qint64>& ids);
    void removeRoot(qint64 id);
    [[nodiscard]] QVector<LibraryRoot> roots() const;
    void rescanAll();
    [[nodiscard]] bool scanBusy() const
    {
        return m_scanning || !m_pendingRoots.isEmpty() || m_checkingLoose;
    }

    [[nodiscard]] static QString orderSql(const QString& column, bool descending);
    [[nodiscard]] QVector<Track> allTracks(const QString& sortColumn, bool descending) const;
    [[nodiscard]] QVector<Track> albumTracks(const QString& albumKey) const;
    [[nodiscard]] QVector<Track> artistTracks(const QString& artistKey) const;
    [[nodiscard]] QVector<Track> folderTracks(qint64 rootId) const;
    [[nodiscard]] QVector<Album> albums(const QString& artistKey = {}) const;
    [[nodiscard]] QVector<Artist> artists() const;
    [[nodiscard]] Track track(qint64 id) const override;
    [[nodiscard]] QVector<Track> tracks(const QVector<qint64>& ids) const override;
    bool saveTags(qint64 id, const TagEdit& edit, QString* error);

    [[nodiscard]] static SearchResults searchDatabase(const QString& path, const QString& text);
    [[nodiscard]] QVector<Track> searchTracks(const QString& text) const;

    [[nodiscard]] qint64 favoritesPlaylistId() const;
    [[nodiscard]] bool isFavorite(qint64 id) const { return m_favorites.contains(id); }
    [[nodiscard]] const QSet<qint64>& favoriteIds() const { return m_favorites; }
    bool setFavorites(const QVector<qint64>& ids, bool liked);
    [[nodiscard]] QVector<Playlist> playlists() const;
    [[nodiscard]] QVector<PlaylistFolder> playlistFolders() const;
    qint64 createPlaylist(const QString& name, qint64 folderId = 0);
    qint64 createPlaylistFolder(const QString& name, qint64 parentId = 0);
    void renamePlaylist(qint64 id, const QString& name);
    void renamePlaylistFolder(qint64 id, const QString& name);
    void deletePlaylist(qint64 id);
    void deletePlaylistFolder(qint64 id);
    void movePlaylistToFolder(qint64 id, qint64 folderId);
    [[nodiscard]] QVector<Track> playlistTracks(qint64 id) const;
    [[nodiscard]] QVector<qint64> playlistTrackIds(qint64 id) const;
    void addToPlaylist(qint64 playlistId, const QVector<qint64>& trackIds);
    void removePlaylistPositions(qint64 playlistId, const QVector<int>& positions);
    void movePlaylistItem(qint64 playlistId, int from, int to);
    void undo();
    void redo();

    bool importM3u(qint64 playlistId, const QString& path, QString* error);
    [[nodiscard]] QVector<qint64> importFiles(const QStringList& paths);
    bool exportM3u(qint64 playlistId, const QString& path, QString* error) const;

    [[nodiscard]] QString setting(const QString& key, const QString& fallback = {}) const override;
    void setSetting(const QString& key, const QString& value) override;

signals:
    void favoritesChanged();
    void favoritesFailed(const QString& message);
    void scanStatistics(qint64 id, int parsedFiles);
    void scanWarning(const QString& message);
    void scanProgress(int done, int total);
    void scanFinished();
    void scanFailed(const QString& message);

private:
    struct UndoCommand {
        std::function<bool()> apply;
        std::function<bool()> revert;
    };

    void remember(UndoCommand command);
    void refreshFavorites();
    void scanRoot(qint64 id, const QString& path);
    void startNextScan();
    void refreshLooseFiles();
    void finishScan(bool changed = true);
    void watchRoots();
    [[nodiscard]] qint64 importResolvedFile(const QString& normalizedPath);

    std::unique_ptr<Database> m_database;
    std::unique_ptr<ArtworkCache> m_artwork;
    QThread* m_scanThread = nullptr;
    Scanner* m_scanner = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    QTimer m_maxWaitTimer;
    QTimer m_verifyTimer;
    QSet<qint64> m_pendingRoots;
    qint64 m_activeRoot = 0;
    qint64 m_lastServedRoot = 0;
    bool m_scanning = false;
    bool m_checkingLoose = false;
    QVector<UndoCommand> m_undo;
    QVector<UndoCommand> m_redo;
    bool m_applyingUndo = false;
    QSet<qint64> m_favorites;
};

} // namespace echo
