#pragma once

#include "library/types.h"

#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <functional>

namespace echo {
class Library;

struct PresentedPage {
    QVector<Track> tracks;
    QVector<QVariantMap> trackRows;
    QVector<QVariantMap> cards;
    qint64 duration = 0;
    QString error;
};

// Page, navigation, search and scroll memory. Row models stay on Client.
class LibraryPresenter : public QObject {
    Q_OBJECT
public:
    struct Route {
        QString kind = QStringLiteral("songs");
        QString key;
        QString title = QStringLiteral("歌曲");
        int sort = 0;
        bool descending = false;
    };

    explicit LibraryPresenter(Library* library, QObject* parent = nullptr);
    void flushColumns();

    [[nodiscard]] const Route& route() const { return m_route; }
    [[nodiscard]] QString routeKey() const;
    [[nodiscard]] QString searchText() const { return m_search; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] bool canBack() const { return !m_history.isEmpty(); }
    [[nodiscard]] const QVector<Track>& pageTracks() const { return m_pageTracks; }
    [[nodiscard]] qint64 totalDuration() const { return m_totalDuration; }
    [[nodiscard]] QString sourcePath() const { return m_sourcePath; }
    [[nodiscard]] QVariantList columns() const { return m_columns; }
    [[nodiscard]] double scroll() const;
    [[nodiscard]] QString pendingPlay() const { return m_pendingPlay; }
    [[nodiscard]] bool pendingShuffle() const { return m_pendingShuffle; }
    void clearPendingPlay() { m_pendingPlay.clear(); }
    void armPlay(const QString& route, bool shuffle);

    using RouteHook = std::function<void(const QString& routeKey)>;
    bool navigate(const QString& kind, const QString& key, const QString& title, const RouteHook& stash, const RouteHook& restore);
    bool back(const RouteHook& stash, const RouteHook& restore, const std::function<void()>& clearSelection);
    void search(const QString& text, const std::function<void()>& clearSelection);
    void refreshSourcePath();
    QVector<QVariantMap> rebuildNavigation();
    void sortBy(int column);
    void requestPage();
    void setScroll(double offset);
    void resizeColumn(int column, double width);
    void setApply(std::function<void(const PresentedPage&)> apply);
    void invalidate();
    void shutdown();

signals:
    void pageChanged();
    void navigationStarted();
    void columnsChanged();

private:
    [[nodiscard]] static double clampedColumnWidth(int column, double width);
    void saveColumns();
    Library* m_library;
    Route m_route;
    Route m_beforeSearch;
    QVector<Route> m_history;
    QHash<QString, double> m_scrolls;
    QVector<Track> m_pageTracks;
    qint64 m_totalDuration = 0;
    QString m_sourcePath;
    QString m_search;
    QString m_pendingPlay;
    bool m_pendingShuffle = false;
    bool m_busy = false;
    quint64 m_generation = 0;
    quint64 m_runningGeneration = 0;
    QThreadPool m_pool;
    QFutureWatcher<PresentedPage> m_query;
    QTimer m_navigationTimer;
    QTimer m_searchTimer;
    QTimer m_columnTimer;
    QVariantList m_columns { 46., 390., 240., 240., 76. };
    std::function<void(const PresentedPage&)> m_apply;
};

} // namespace echo
