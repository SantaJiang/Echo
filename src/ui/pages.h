#pragma once

#include "library/library.h"
#include "library/types.h"
#include "ui/tracktable.h"

#include <QWidget>
#include <QHash>

class QLabel;
class QPushButton;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTreeWidget;

namespace echo {

class TrackListPage : public QWidget {
    Q_OBJECT
public:
    enum class Source { AllSongs, Folder, Playlist };
    TrackListPage(Library* library, Source source, QWidget* parent = nullptr);
    void setSource(qint64 id);
    void reload();
    void setPlayingId(qint64 id, bool playing)
    {
        m_table->setPlayingId(id, playing);
    }
    [[nodiscard]] TrackTable* table() const { return m_table; }
    [[nodiscard]] QString title() const
    {
        return m_title;
    }
    [[nodiscard]] qint64 sourceId() const
    {
        return m_id;
    }
    [[nodiscard]] Source source() const
    {
        return m_source;
    }
signals:
    void titleChanged(const QString& title);
    void addFolderRequested();
    void playAllRequested(const QVector<qint64>& ids, bool shuffle);

protected:
    Library* m_library;
    TrackTable* m_table;
    QLabel* m_empty;

private:
    void saveView();
    QString viewKey() const;
    Source m_source;
    qint64 m_id = 0;
    QString m_title;
    QLabel* m_subtitle;
    QLabel* m_path;
    QPushButton* m_add;
    int m_sort = 0;
    bool m_desc = false;
    bool m_changingSource = false;
    struct View {
        int sort = 0;
        bool desc = false;
        int scroll = 0;
        QVector<qint64> selected;
    };
    QHash<qint64, View> m_views;
};

class SongsPage : public TrackListPage {
    Q_OBJECT
public:
    explicit SongsPage(Library* library, QWidget* parent = nullptr)
        : TrackListPage(library, Source::AllSongs, parent)
    {
    }
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
    void artistPlayRequested(const QString& key, bool shuffle);
    void albumPlayRequested(const QString& albumKey, bool shuffle);

private:
    void showArtistRow(int row);
    QString m_selectedArtist;
    QWidget* m_details = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_shuffle = nullptr;
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
    void openFolder(qint64 id);
    void selectFolder(qint64 id);
    void showFolders();
    void setPlayingId(qint64 id, bool playing)
    {
        m_detail->setPlayingId(id, playing);
    }
    [[nodiscard]] TrackTable* table() const
    {
        return m_detail->table();
    }
    [[nodiscard]] TrackListPage* detailPage() const
    {
        return m_detail;
    }
    [[nodiscard]] QString title() const;
signals:
    void titleChanged(const QString& title);
    void addFolderRequested();
    void playlistCreated(qint64 id);

private:
    Library* m_library;
    QTreeWidget* m_tree;
    QStackedWidget* m_stack;
    TrackListPage* m_detail;
    QLabel* m_empty;
};

class PlaylistPage : public TrackListPage {
    Q_OBJECT
public:
    explicit PlaylistPage(Library* library, QWidget* parent = nullptr)
        : TrackListPage(library, Source::Playlist, parent)
    {
    }
    void setPlaylist(qint64 id)
    {
        setSource(id);
    }
    [[nodiscard]] qint64 playlistId() const
    {
        return sourceId();
    }
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
    void launchSearch();
    void applySearch(const SearchResults& result, const QString& text);
    QTimer* m_searchTimer = nullptr;
    QString m_query;
    quint64 m_searchRevision = 0;
    bool m_searchRunning = false;
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
