#pragma once

#include "library/types.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace echo {

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
    void setPlaylists(const QVector<PlaylistFolder>& folders, const QVector<Playlist>& playlists);
    void setScanText(const QString& text);
    void setSelectedLibrary(int index);

signals:
    void librarySelected(int index);
    void playlistSelected(qint64 id);
    void createPlaylistRequested(qint64 folderId);
    void createFolderRequested();
    void renamePlaylistRequested(qint64 id);
    void deletePlaylistRequested(qint64 id);
    void renameFolderRequested(qint64 id);
    void deleteFolderRequested(qint64 id);
    void addFolderRequested();

private:
    void selectLibrary(int index);
    QListWidget* m_library = nullptr;
    QTreeWidget* m_playlists = nullptr;
    QLabel* m_playlistEmpty = nullptr;
    QLabel* m_status = nullptr;
    bool m_rebuilding = false;
};

} // namespace echo
