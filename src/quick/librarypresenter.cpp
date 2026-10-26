#include "quick/librarypresenter.h"

#include "library/library.h"
#include "library/pagequery.h"
#include "library/settingskeys.h"
#include "presentation/theme.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>

namespace echo {
namespace {
QString artUrl(const QString& base, const QString& hash)
{
    const QString path = base + hash.left(2) + u'/' + hash;
    return hash.isEmpty() || !QFileInfo::exists(path) ? QString() : QUrl::fromLocalFile(path).toString();
}

QVariantMap trackRow(const Track& track, const QString& base)
{
    return { { QStringLiteral("key"),
                 track.listEntryId ? QStringLiteral("entry:") + QString::number(track.listEntryId) : QString::number(track.id) },
        { QStringLiteral("id"), track.id }, { QStringLiteral("title"), track.displayTitle() },
        { QStringLiteral("artist"), track.displayArtist() }, { QStringLiteral("album"), track.displayAlbum() },
        { QStringLiteral("duration"), formatDuration(track.durationMs) }, { QStringLiteral("missing"), track.missing },
        { QStringLiteral("artwork"), artUrl(base, track.artworkHash) }, { QStringLiteral("position"), track.playlistPosition } };
}

QVector<QVariantMap> albumRows(const QVector<Album>& albums, const QString& base)
{
    QVector<QVariantMap> rows;
    rows.reserve(albums.size());
    for (const auto& album : albums)
        rows << QVariantMap { { QStringLiteral("key"), album.key }, { QStringLiteral("kind"), QStringLiteral("album") },
            { QStringLiteral("title"), album.title }, { QStringLiteral("subtitle"), album.artist },
            { QStringLiteral("artwork"), artUrl(base, album.artworkHash) }, { QStringLiteral("count"), album.tracks } };
    return rows;
}

PresentedPage readPage(const QString& path, const QString& kind, const QString& key, const QString& search, int sort,
    bool descending, const QString& artBase)
{
    const LibraryPage page = queryLibraryPage(path, kind, key, search, sort, descending);
    PresentedPage result;
    result.error = page.error;
    result.tracks = page.tracks;
    result.cards = albumRows(page.albums, artBase);
    for (const auto& artist : page.artists)
        result.cards << QVariantMap { { QStringLiteral("key"), artist.key }, { QStringLiteral("kind"), QStringLiteral("artist") },
            { QStringLiteral("title"), artist.name },
            { QStringLiteral("subtitle"), QStringLiteral("%1 张专辑 · %2 首歌曲").arg(artist.albums).arg(artist.tracks) },
            { QStringLiteral("artwork"), QString() } };
    for (const auto& playlist : page.playlists)
        result.cards << QVariantMap { { QStringLiteral("key"), QString::number(playlist.id) },
            { QStringLiteral("kind"), QStringLiteral("playlist") }, { QStringLiteral("title"), playlist.name },
            { QStringLiteral("subtitle"), QStringLiteral("播放列表") }, { QStringLiteral("artwork"), QString() } };
    for (const auto& root : page.roots)
        result.cards << QVariantMap { { QStringLiteral("key"), QString::number(root.id) },
            { QStringLiteral("kind"), QStringLiteral("source") }, { QStringLiteral("title"), root.name },
            { QStringLiteral("subtitle"),
                QStringLiteral("%1 首歌曲 · %2")
                    .arg(root.tracks)
                    .arg(root.status == QLatin1String("available") ? QStringLiteral("可用") : QStringLiteral("暂时不可用")) },
            { QStringLiteral("path"), root.path }, { QStringLiteral("artwork"), QString() },
            { QStringLiteral("missing"), root.status != QLatin1String("available") } };
    result.trackRows.reserve(result.tracks.size());
    for (const auto& track : result.tracks) {
        result.trackRows << trackRow(track, artBase);
        result.duration += track.durationMs;
    }
    return result;
}
}

LibraryPresenter::LibraryPresenter(Library* library, QObject* parent)
    : QObject(parent)
    , m_library(library)
{
    m_pool.setMaxThreadCount(1);
    const auto widths = library->setting(settings::trackColumns).split(u',');
    if (widths.size() == 5)
        for (int i = 0; i < 5; ++i)
            m_columns[i] = clampedColumnWidth(i, widths[i].toDouble());
    m_columnTimer.setSingleShot(true);
    m_columnTimer.setInterval(200);
    connect(&m_columnTimer, &QTimer::timeout, this, &LibraryPresenter::saveColumns);
    m_navigationTimer.setSingleShot(true);
    m_navigationTimer.setInterval(120);
    connect(&m_navigationTimer, &QTimer::timeout, this, &LibraryPresenter::requestPage);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(180);
    connect(&m_searchTimer, &QTimer::timeout, this, &LibraryPresenter::requestPage);
    connect(&m_query, &QFutureWatcher<PresentedPage>::finished, this, [this] {
        if (m_runningGeneration != m_generation) {
            requestPage();
            return;
        }
        const auto result = m_query.result();
        m_busy = false;
        if (!result.error.isEmpty()) {
            if (m_apply)
                m_apply(result);
            emit pageChanged();
            return;
        }
        m_pageTracks = result.tracks;
        m_totalDuration = result.duration;
        if (m_apply)
            m_apply(result);
    });
}

void LibraryPresenter::invalidate()
{
    ++m_generation;
}

void LibraryPresenter::shutdown()
{
    flushColumns();
    m_query.waitForFinished();
    m_pool.waitForDone();
}

void LibraryPresenter::flushColumns()
{
    if (!m_columnTimer.isActive())
        return;
    m_columnTimer.stop();
    saveColumns();
}

void LibraryPresenter::setApply(std::function<void(const PresentedPage&)> apply)
{
    m_apply = std::move(apply);
}

QString LibraryPresenter::routeKey() const
{
    return m_route.kind + QLatin1Char(':') + m_route.key;
}

double LibraryPresenter::scroll() const
{
    return m_scrolls.value(routeKey());
}

void LibraryPresenter::armPlay(const QString& route, bool shuffle)
{
    m_pendingPlay = route;
    m_pendingShuffle = shuffle;
}

bool LibraryPresenter::navigate(const QString& kind, const QString& key, const QString& title, const RouteHook& stash,
    const RouteHook& restore)
{
    static const QSet<QString> allowed { QStringLiteral("songs"), QStringLiteral("albums"), QStringLiteral("artists"),
        QStringLiteral("sources"), QStringLiteral("album"), QStringLiteral("artist"), QStringLiteral("source"),
        QStringLiteral("playlist") };
    if (!allowed.contains(kind) || (kind == m_route.kind && key == m_route.key))
        return false;
    const auto previous = routeKey();
    if (stash)
        stash(previous);
    m_history << m_route;
    m_route = { kind, key, title.isEmpty() ? kind : title };
    m_search.clear();
    m_searchTimer.stop();
    m_pendingPlay.clear();
    m_sourcePath = kind == QLatin1String("source") ? m_library->root(key.toLongLong()).path : QString();
    if (restore)
        restore(routeKey());
    ++m_generation;
    m_busy = true;
    m_navigationTimer.start();
    emit navigationStarted();
    return true;
}

bool LibraryPresenter::back(const RouteHook& stash, const RouteHook& restore, const std::function<void()>& clearSelection)
{
    if (m_route.kind == QLatin1String("search")) {
        search({}, clearSelection);
        return true;
    }
    if (m_history.isEmpty())
        return false;
    const auto previous = routeKey();
    if (stash)
        stash(previous);
    m_route = m_history.takeLast();
    if (restore)
        restore(routeKey());
    m_search = m_route.kind == QLatin1String("search") ? m_route.key : QString();
    m_sourcePath = m_route.kind == QLatin1String("source") ? m_library->root(m_route.key.toLongLong()).path : QString();
    m_searchTimer.stop();
    m_pendingPlay.clear();
    ++m_generation;
    m_busy = true;
    m_navigationTimer.start();
    emit navigationStarted();
    return true;
}

void LibraryPresenter::search(const QString& text, const std::function<void()>& clearSelection)
{
    const QString query = text.trimmed();
    if (m_search == query)
        return;
    if (m_route.kind != QLatin1String("search"))
        m_beforeSearch = m_route;
    m_search = query;
    m_searchTimer.stop();
    m_navigationTimer.stop();
    m_busy = true;
    emit navigationStarted();
    ++m_generation;
    m_pendingPlay.clear();
    if (clearSelection)
        clearSelection();
    if (query.isEmpty()) {
        m_route = m_beforeSearch;
        m_navigationTimer.start();
        return;
    }
    m_route = { QStringLiteral("search"), query, QStringLiteral("搜索结果") };
    m_busy = true;
    m_searchTimer.start();
}

void LibraryPresenter::refreshSourcePath()
{
    if (m_route.kind == QLatin1String("source"))
        m_sourcePath = m_library->root(m_route.key.toLongLong()).path;
}

QVector<QVariantMap> LibraryPresenter::rebuildNavigation()
{
    QVector<QVariantMap> rows;
    const QStringList kinds { QStringLiteral("songs"), QStringLiteral("albums"), QStringLiteral("artists"),
        QStringLiteral("sources") };
    const QStringList titles { QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺术家"), QStringLiteral("文件夹") };
    for (int i = 0; i < kinds.size(); ++i)
        rows << QVariantMap { { QStringLiteral("key"), kinds[i] }, { QStringLiteral("kind"), kinds[i] },
            { QStringLiteral("title"), titles[i] }, { QStringLiteral("icon"), i == 3 ? QStringLiteral("folders") : kinds[i] },
            { QStringLiteral("depth"), 0 } };
    const auto lists = m_library->playlists();
    const auto folders = m_library->playlistFolders();
    rows << QVariantMap { { QStringLiteral("key"), QStringLiteral("playlistSection") },
        { QStringLiteral("kind"), QStringLiteral("playlistSection") }, { QStringLiteral("title"), QStringLiteral("播放列表") },
        { QStringLiteral("icon"), QString() }, { QStringLiteral("depth"), 0 } };
    for (const auto& playlist : lists)
        if (playlist.isFavorites())
            rows << QVariantMap { { QStringLiteral("key"), QString::number(playlist.id) },
                { QStringLiteral("kind"), QStringLiteral("playlist") }, { QStringLiteral("title"), playlist.name },
                { QStringLiteral("icon"), QStringLiteral("heartFilled") }, { QStringLiteral("depth"), 0 },
                { QStringLiteral("protected"), true } };
    QSet<qint64> visited;
    std::function<void(qint64, int)> append = [&](qint64 parent, int depth) {
        for (const auto& folder : folders)
            if (folder.parentId == parent && !visited.contains(folder.id)) {
                visited.insert(folder.id);
                rows << QVariantMap { { QStringLiteral("key"), QString::number(folder.id) },
                    { QStringLiteral("kind"), QStringLiteral("listFolder") }, { QStringLiteral("title"), folder.name },
                    { QStringLiteral("icon"), QStringLiteral("folders") }, { QStringLiteral("depth"), depth } };
                append(folder.id, depth + 1);
            }
        for (const auto& playlist : lists)
            if (!playlist.isFavorites() && playlist.folderId == parent)
                rows << QVariantMap { { QStringLiteral("key"), QString::number(playlist.id) },
                    { QStringLiteral("kind"), QStringLiteral("playlist") }, { QStringLiteral("title"), playlist.name },
                    { QStringLiteral("icon"), QStringLiteral("playlist") }, { QStringLiteral("depth"), depth } };
    };
    append(0, 0);
    if (m_route.kind == QLatin1String("playlist"))
        for (const auto& playlist : lists)
            if (QString::number(playlist.id) == m_route.key) {
                m_route.title = playlist.name;
                break;
            }
    return rows;
}

void LibraryPresenter::sortBy(int column)
{
    if (column < 0 || column > 4)
        return;
    if (m_route.sort == column)
        m_route.descending = !m_route.descending;
    else {
        m_route.sort = column;
        m_route.descending = false;
    }
    ++m_generation;
    requestPage();
}

void LibraryPresenter::requestPage()
{
    if (m_searchTimer.isActive() || m_navigationTimer.isActive())
        return;
    if (m_query.isRunning()) {
        ++m_generation;
        return;
    }
    m_busy = true;
    m_runningGeneration = ++m_generation;
    emit pageChanged();
    const auto route = m_route;
    const auto query = m_search;
    const auto path = m_library->databasePath();
    const auto base = m_library->artwork()->filePath({});
    m_query.setFuture(QtConcurrent::run(&m_pool, [path, route, query, base] {
        return readPage(path, route.kind, route.key, query, route.sort, route.descending, base);
    }));
}

void LibraryPresenter::setScroll(double offset)
{
    m_scrolls.insert(routeKey(), offset);
}

void LibraryPresenter::resizeColumn(int column, double width)
{
    if (column < 0 || column >= 5)
        return;
    m_columns[column] = clampedColumnWidth(column, width);
    emit columnsChanged();
    m_columnTimer.start();
}

double LibraryPresenter::clampedColumnWidth(int column, double width)
{
    return std::clamp(width, column == 0 ? 40. : 64., 1200.);
}

void LibraryPresenter::saveColumns()
{
    QStringList widths;
    for (const auto& value : m_columns)
        widths << QString::number(value.toInt());
    m_library->setSetting(settings::trackColumns, widths.join(u','));
}

} // namespace echo
