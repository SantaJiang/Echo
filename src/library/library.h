#pragma once

#include "library/artwork.h"
#include "library/database.h"
#include "library/types.h"

#include <QFileSystemWatcher>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <memory>

namespace echo {

class Scanner;

class Library : public QObject {
    Q_OBJECT

public:
    explicit Library(QObject* parent = nullptr);
    ~Library() override;

    bool open(QString* error = nullptr);
    [[nodiscard]] ArtworkCache* artwork() const { return m_artwork.get(); }
    [[nodiscard]] QString databasePath() const { return m_database ? m_database->filePath() : QString(); }

    void addRoot(const QString& path);
    void removeRoot(qint64 id);
    [[nodiscard]] QVector<LibraryRoot> roots() const;
    void rescanAll();

    [[nodiscard]] QVector<Track> allTracks(const QString& sortColumn, bool descending) const;
    [[nodiscard]] QVector<Track> albumTracks(const QString& albumKey) const;
    [[nodiscard]] QVector<Track> artistTracks(const QString& artistKey) const;
    [[nodiscard]] QVector<Track> folderTracks(const QString& folder, bool recursive) const;
    [[nodiscard]] QVector<Album> albums(const QString& artistKey = {}) const;
    [[nodiscard]] QVector<Artist> artists() const;
    [[nodiscard]] QVector<QString> folders() const;
    [[nodiscard]] Track track(qint64 id) const;
    [[nodiscard]] QVector<Track> tracks(const QVector<qint64>& ids) const;
    void removeFromLibrary(const QVector<qint64>& ids);
    bool saveTags(qint64 id, const TagEdit& edit, QString* error);

    [[nodiscard]] QVector<Track> searchTracks(const QString& text) const;
    [[nodiscard]] QVector<Album> searchAlbums(const QString& text) const;
    [[nodiscard]] QVector<Artist> searchArtists(const QString& text) const;
    [[nodiscard]] QVector<Playlist> searchPlaylists(const QString& text) const;

    [[nodiscard]] QVector<Playlist> playlists() const;
    [[nodiscard]] QVector<PlaylistFolder> playlistFolders() const;
    qint64 createPlaylist(const QString& name, qint64 folderId = 0);
    qint64 createPlaylistFolder(const QString& name, qint64 parentId = 0);
    void renamePlaylist(qint64 id, const QString& name);
    void renamePlaylistFolder(qint64 id, const QString& name);
    void deletePlaylist(qint64 id);
    void deletePlaylistFolder(qint64 id);
    void movePlaylistToFolder(qint64 id, qint64 folderId);
    [[nodiscard]] QVector<qint64> playlistTrackIds(qint64 id) const;
    void addToPlaylist(qint64 playlistId, const QVector<qint64>& trackIds);
    void removePlaylistPositions(qint64 playlistId, const QVector<int>& positions);
    void movePlaylistItem(qint64 playlistId, int from, int to);
    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const { return !m_undo.isEmpty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }

    bool importM3u(qint64 playlistId, const QString& path, QString* error);
    [[nodiscard]] QVector<qint64> importFiles(const QStringList& paths);
    bool exportM3u(qint64 playlistId, const QString& path, QString* error) const;

    [[nodiscard]] QString setting(const QString& key, const QString& fallback = {}) const;
    void setSetting(const QString& key, const QString& value);

signals:
    void changed();
    void scanProgress(int done, int total);
    void scanFinished();
    void scanFailed(const QString& message);

private:
    struct UndoStep {
        enum Kind { Add, Remove, Move, RenamePlaylist, RenameFolder, DeletePlaylist, CreatePlaylist, CreateFolder } kind;
        qint64 playlistId = 0;
        qint64 folderId = 0;
        QString name;
        QString previousName;
        QVector<qint64> trackIds;
        QVector<int> positions;
        int from = 0;
        int to = 0;
    };

    void remember(const UndoStep& step);
    void apply(const UndoStep& step, bool inverse);
    void scanRoot(qint64 id, const QString& path);
    void watchRoots();
    [[nodiscard]] static QString orderSql(const QString& column, bool descending);

    std::unique_ptr<Database> m_database;
    std::unique_ptr<ArtworkCache> m_artwork;
    QThread* m_scanThread = nullptr;
    Scanner* m_scanner = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    qint64 m_pendingRoot = 0;
    QVector<UndoStep> m_undo;
    QVector<UndoStep> m_redo;
    bool m_applyingUndo = false;
};

} // namespace echo
