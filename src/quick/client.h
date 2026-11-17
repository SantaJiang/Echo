#pragma once
#include "library/types.h"
#include "playback/player.h"
#include "quick/dialogservice.h"
#include "quick/librarypresenter.h"
#include "quick/menuservice.h"
#include "quick/rowmodel.h"
#include "quick/selectionmodel.h"
#include "quick/systemintegration.h"
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>

class QQuickView;
namespace echo {
struct UiMenu;
class QuickView;
class Client : public QObject {
    Q_OBJECT
    Q_PROPERTY(echo::RowModel* tracks READ tracks CONSTANT)
    Q_PROPERTY(echo::RowModel* cards READ cards CONSTANT)
    Q_PROPERTY(echo::RowModel* navigation READ navigation CONSTANT)
    Q_PROPERTY(echo::RowModel* queue READ queue CONSTANT)
    Q_PROPERTY(QVariantMap page READ page NOTIFY pageChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(QVariantMap dialog READ dialog NOTIFY dialogChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
public:
    Client(Library* library, Player* player, AudioEngine* engine, QObject* parent = nullptr);
    ~Client() override;
    void attach(QuickView* window);
    QVariantMap dialog() const { return m_dialogs.current(); }
    QVariantMap preferences() const;
    Q_INVOKABLE void setPreference(const QString& key, bool value);
    Q_INVOKABLE void answerDialog(int token, const QVariantMap& answer);
    Q_INVOKABLE void triggerMenu(int token);
    Q_INVOKABLE QPointF cursorPosition() const;

    RowModel* tracks() { return &m_tracks; }
    RowModel* cards() { return &m_cards; }
    RowModel* navigation() { return &m_navigation; }
    RowModel* queue() { return &m_queue; }
    QVariantMap page() const;
    QString status() const { return m_status; }
    int selectionRevision() const { return m_selection.revision(); }
    int selectedCount() const { return m_selection.count(); }
    bool reduceMotion() const { return m_reduceMotion; }
    void setReduceMotion(bool value);
    QVariantList columns() const { return m_pages.columns(); }
    Q_INVOKABLE void resizeColumn(int column, double width);
    Q_INVOKABLE void navigate(const QString& kind, const QString& key = {}, const QString& title = {});
    Q_INVOKABLE void back();
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openCard(int row);
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void playAll(bool shuffle = false);
    Q_INVOKABLE void playCard(int row, bool shuffle = false);
    Q_INVOKABLE void sortBy(int column);
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void selectRow(int row, int modifiers = 0);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void favoriteRow(int row);
    Q_INVOKABLE void trackMenu(int row);
    Q_INVOKABLE void cardMenu(int row);
    Q_INVOKABLE void navigationMenu(int row);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void moveTrack(int from, int to);
    Q_INVOKABLE void queueAction(const QString& action, int row = -1, int to = -1);
    Q_INVOKABLE void action(const QString& name);
    Q_INVOKABLE void openUrls(const QVariantList& urls);
    void openPaths(const QStringList& paths);
    Q_INVOKABLE void showMainWindow();
    Q_INVOKABLE void showSettings();
    Q_INVOKABLE void setTheme(const QString& id);
    Q_INVOKABLE void setScroll(double offset);
    Q_INVOKABLE double scroll() const;
signals:
    void dialogChanged();
    void preferencesChanged();
    void themeChanged();
    void settingsRequested();
    void menuRequested(const QVariantList& entries);
    void trayMenuRequested(const QPoint& position);
    void pageChanged();
    void navigationStarted();
    void statusChanged();
    void selectionChanged();
    void reduceMotionChanged();
    void columnsChanged();
    void focusSearch();

private:
    void requestPage();
    void rebuildNavigation();
    void syncQueue(bool force = false);
    void syncTrackState();
    void report(const QString& message);
    void setupMenus();
    void addMusicFolder();
    void importPlaylist();
    void exportPlaylist();
    void createPlaylist(qint64 folder = 0);
    void createFolder();
    void showInfo(qint64 id);
    void locateTrack(qint64 id);
    void showUnavailable(qint64 id);
    void trashTracks(const QVector<qint64>& ids);
    void addToPlaylist(const QVector<qint64>& ids);
    void sourceMenu(qint64 id);
    void playlistMenu(qint64 id);
    void folderMenu(qint64 id);
    void popup(UiMenu& menu);
    void askName(const QString& title, const QString& initial, const QString& accept, std::function<void(QString)> reply);
    void choosePath(const QString& title, const QString& mode, const QStringList& filters, std::function<void(QString)> reply);
    void requestDialog(QVariantMap data, DialogService::Reply reply = {});
    QVector<qint64> selectedIds() const;
    QString rowKey(int row) const;
    QString routeKey() const;
    [[nodiscard]] const LibraryPresenter::Route& route() const { return m_pages.route(); }
    [[nodiscard]] const QVector<Track>& pageTracks() const { return m_pages.pageTracks(); }
    Library* m_library;
    Player* m_player;
    AudioEngine* m_engine;
    LibraryPresenter m_pages;
    SelectionModel m_selection;
    DialogService m_dialogs;
    MenuService m_menus;
    SystemIntegration m_system;
    RowModel m_tracks, m_cards, m_navigation, m_queue;
    QVector<Player::Entry> m_lastQueue;
    int m_lastQueueIndex = -2;
    qint64 m_lastPlayingId = -1;
    QSet<qint64> m_lastFavorites;
    bool m_lastPlaying = false;
    bool m_reduceMotion = false;
    QString m_status;
    QTimer m_refreshTimer;
};
}
