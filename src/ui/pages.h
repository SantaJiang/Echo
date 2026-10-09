#pragma once

#include "library/library.h"
#include "library/types.h"
#include "ui/tracktable.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTreeWidget;

namespace echo {

class SongsPage : public QWidget {
    Q_OBJECT
public:
    explicit SongsPage(Library* library, QWidget* parent = nullptr);
    void reload();
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] TrackTable* table() const { return m_table; }

signals:
    void addFolderRequested();

private:
    Library* m_library = nullptr;
    TrackTable* m_table = nullptr;
    QWidget* m_empty = nullptr;
    QString m_sort = QStringLiteral("album");
    bool m_desc = false;
};

class AlbumsPage : public QWidget {
    Q_OBJECT
public:
    explicit AlbumsPage(Library* library, QWidget* parent = nullptr);
    void reload();
    void showArtist(const QString& artistKey);
    void openAlbum(const QString& albumKey);
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] TrackTable* detailTable() const { return m_detail; }

signals:
    void albumPlayRequested(const QString& albumKey, bool shuffle);
    void artistRequested(const QString& artistKey);

private:
    void showAlbum(const QString& key);
    Library* m_library = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListWidget* m_grid = nullptr;
    QWidget* m_detailPage = nullptr;
    TrackTable* m_detail = nullptr;
    QString m_artistFilter;
};

class ArtistsPage : public QWidget {
    Q_OBJECT
public:
    explicit ArtistsPage(Library* library, QWidget* parent = nullptr);
    void reload();
    void selectArtist(const QString& artistKey);
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] TrackTable* table() const { return m_table; }

signals:
    void albumPlayRequested(const QString& albumKey, bool shuffle);

private:
    Library* m_library = nullptr;
    QListWidget* m_artists = nullptr;
    QListWidget* m_albums = nullptr;
    TrackTable* m_table = nullptr;
};

class FoldersPage : public QWidget {
    Q_OBJECT
public:
    explicit FoldersPage(Library* library, QWidget* parent = nullptr);
    void reload();
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] TrackTable* table() const { return m_table; }

signals:
    void playFolderRequested(const QString& folder);

private:
    Library* m_library = nullptr;
    QTreeWidget* m_tree = nullptr;
    TrackTable* m_table = nullptr;
};

class PlaylistPage : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPage(Library* library, QWidget* parent = nullptr);
    void setPlaylist(qint64 id);
    void reload();
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] qint64 playlistId() const { return m_id; }
    [[nodiscard]] TrackTable* table() const { return m_table; }

private:
    Library* m_library = nullptr;
    TrackTable* m_table = nullptr;
    qint64 m_id = 0;
};

class SearchPage : public QWidget {
    Q_OBJECT
public:
    explicit SearchPage(Library* library, QWidget* parent = nullptr);
    void search(const QString& text);
    void setPlayingId(qint64 id, bool playing);
    [[nodiscard]] TrackTable* table() const { return m_tracks; }

signals:
    void albumSelected(const QString& key);
    void artistSelected(const QString& key);
    void playlistSelected(qint64 id);

private:
    void showSection(QWidget* section, QListWidget* list);
    Library* m_library = nullptr;
    QWidget* m_empty = nullptr;
    QLabel* m_emptyText = nullptr;
    QWidget* m_body = nullptr;
    QWidget* m_trackSection = nullptr;
    QWidget* m_artistSection = nullptr;
    QWidget* m_albumSection = nullptr;
    QWidget* m_playlistSection = nullptr;
    TrackTable* m_tracks = nullptr;
    QListWidget* m_albums = nullptr;
    QListWidget* m_artists = nullptr;
    QListWidget* m_playlists = nullptr;
};

} // namespace echo
