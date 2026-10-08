#pragma once

#include <QMainWindow>
#include <QStringList>

class QLabel;
class QLineEdit;
class QStackedWidget;

namespace echo {

class AudioEngine;
class AlbumsPage;
class ArtistsPage;
class FoldersPage;
class Library;
class MiniPlayer;
class NowPlayingPage;
class Player;
class PlayerBar;
class PlaylistPage;
class QueuePanel;
class SearchPage;
class Sidebar;
class SongsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Library* library, Player* player, AudioEngine* engine, QWidget* parent = nullptr);
    void openPaths(const QStringList& paths);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void refreshViews();
    void showLibraryPage(int index);
    void syncPlayer();
    void playVisible(const QVector<qint64>& ids, int index, bool albumContext);
    void addToPlaylist(const QVector<qint64>& ids);
    void showInfo(qint64 id);
    void reveal(qint64 id);
    void promptPlaylist(qint64 folderId);
    void promptFolder();
    [[nodiscard]] class TrackTable* activeTable() const;

    Library* m_library = nullptr;
    Player* m_player = nullptr;
    AudioEngine* m_engine = nullptr;
    Sidebar* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_heading = nullptr;
    SongsPage* m_songs = nullptr;
    AlbumsPage* m_albums = nullptr;
    ArtistsPage* m_artists = nullptr;
    FoldersPage* m_folders = nullptr;
    PlaylistPage* m_playlist = nullptr;
    SearchPage* m_searchPage = nullptr;
    NowPlayingPage* m_now = nullptr;
    PlayerBar* m_bar = nullptr;
    QueuePanel* m_queue = nullptr;
    MiniPlayer* m_mini = nullptr;
    int m_libraryPage = 0;
    qint64 m_playlistId = 0;
    bool m_showingSearch = false;
};

} // namespace echo
