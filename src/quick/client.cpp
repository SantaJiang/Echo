#include "quick/client.h"
#include "library/library.h"
#include "library/settingskeys.h"
#include "platform/platform.h"
#include "quick/quickview.h"
#include "quick/uimenu.h"
#include "presentation/theme.h"
#include <QFile>
#include <QImageReader>
#include <QFileInfo>
#include <QUrl>

namespace echo {
namespace {
    QString artUrl(const QString& base, const QString& hash)
    {
        const QString path = base + hash.left(2) + u'/' + hash;
        return hash.isEmpty() || !QFileInfo::exists(path) ? QString() : QUrl::fromLocalFile(path).toString();
    }
    QVariantMap trackRow(const Track& track, const QString& base)
    {
        return { { "key",
                     track.listEntryId ? "entry:" + QString::number(track.listEntryId) : QString::number(track.id) },
            { "id", track.id }, { "title", track.displayTitle() }, { "artist", track.displayArtist() },
            { "album", track.displayAlbum() }, { "duration", formatDuration(track.durationMs) },
            { "missing", track.missing }, { "artwork", artUrl(base, track.artworkHash) },
            { "position", track.playlistPosition } };
    }
}

Client::Client(Library* library, Player* player, AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_library(library)
    , m_player(player)
    , m_engine(engine)
    , m_pages(library, this)
    , m_dialogs(this)
    , m_system(this)
{
    m_reduceMotion = library->setting(settings::reduceMotion, QStringLiteral("0")) == QLatin1String("1");
    m_pages.setApply([this](const PresentedPage& result) {
        if (!result.error.isEmpty()) {
            report(result.error);
            return;
        }
        m_tracks.replace(result.trackRows);
        m_tracks.setTrackState(m_library->favoriteIds(), m_player->currentTrack().id);
        m_cards.replace(result.cards);
        m_selection.retain(m_tracks.count(), [this](int row) { return rowKey(row); });
        emit selectionChanged();
        emit pageChanged();
        if (!m_pages.pendingPlay().isEmpty() && m_pages.pendingPlay() == m_pages.routeKey()) {
            const bool shuffle = m_pages.pendingShuffle();
            m_pages.clearPendingPlay();
            playAll(shuffle);
        }
    });
    connect(&m_pages, &LibraryPresenter::pageChanged, this, &Client::pageChanged);
    connect(&m_pages, &LibraryPresenter::navigationStarted, this, &Client::navigationStarted);
    connect(&m_pages, &LibraryPresenter::columnsChanged, this, &Client::columnsChanged);
    connect(&m_dialogs, &DialogService::changed, this, &Client::dialogChanged);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, &Client::refresh);
    auto changed = [this] {
        m_pages.invalidate();
        m_refreshTimer.start();
    };
    connect(library, &Library::changed, this, changed);
    connect(library, &Library::sourceChanged, this, [changed](qint64) { changed(); });
    connect(library, &Library::favoritesChanged, this, [this] {
        syncTrackState();
        if (route().kind == QLatin1String("playlist") && route().key.toLongLong() == m_library->favoritesPlaylistId()) {
            m_pages.invalidate();
            m_refreshTimer.start();
        }
    });
    connect(library, &Library::favoritesFailed, this, &Client::report);
    connect(library, &Library::scanProgress, this, [this](int done, int total) {
        if (total > done)
            report(QStringLiteral("正在扫描 %1 / %2").arg(done).arg(total));
    });
    connect(library, &Library::scanFinished, this, [this] { report({}); });
    connect(library, &Library::scanFailed, this, &Client::report);
    connect(library, &Library::scanWarning, this, &Client::report);
    connect(player, &Player::playbackMessage, this, &Client::report);
    connect(player, &Player::trackUnavailable, this, &Client::showUnavailable);
    connect(player, &Player::trashFinished, this, [this](const QString& text) {
        requestDialog({{"kind", "message"}, {"title", QStringLiteral("移到废纸篓")},
            {"text", text.section(u'\n', 0, 0)}, {"details", text.section(u'\n', 1)}});
    });
    connect(engine, &AudioEngine::errorOccurred, this, &Client::report);
    connect(engine, &AudioEngine::exclusiveFallback, this,
        [this](const QString& reason) { report(QStringLiteral("已回退共享输出：") + reason); });
    connect(player, &Player::changed, this, [this] {
        syncTrackState();
        syncQueue();
    });
    connect(player, &Player::changed, this, &Client::preferencesChanged);
    connect(player, &Player::normalizationChanged, this, &Client::preferencesChanged);
    connect(player, &Player::spatialChanged, this, &Client::preferencesChanged);
    connect(this, &Client::reduceMotionChanged, this, &Client::preferencesChanged);
    setupMenus();
    rebuildNavigation();
    requestPage();
}
Client::~Client()
{
    m_pages.shutdown();
}
void Client::attach(QuickView* window)
{
    m_system.attach(window, SystemIntegration::Hooks {
        [this] { showSettings(); },
        [this] { addMusicFolder(); },
        [this] { createPlaylist(); },
        [this] { emit focusSearch(); },
        [this](int token) { triggerMenu(token); },
        [this] { return m_menus.describe(m_menus.bar(), true); },
        [this](const QPoint& position) { emit trayMenuRequested(position); },
        [this] { return m_dialogs.isOpen(); },
        m_player,
        m_library,
        m_engine,
    });
}
QVariantMap Client::page() const
{
    const auto& current = route();
    QString subtitle = QStringLiteral("%1 首歌曲 · %2").arg(pageTracks().size()).arg(formatDuration(m_pages.totalDuration()));
    if (current.kind == QLatin1String("albums") || current.kind == QLatin1String("artists") || current.kind == QLatin1String("sources"))
        subtitle = QStringLiteral("%1 个项目").arg(m_cards.count());
    if (current.kind == QLatin1String("source"))
        subtitle = m_pages.sourcePath() + QStringLiteral(" · ") + subtitle;
    return { { QStringLiteral("kind"), current.kind }, { QStringLiteral("key"), current.key },
        { QStringLiteral("title"), current.title }, { QStringLiteral("subtitle"), subtitle },
        { QStringLiteral("busy"), m_pages.busy() }, { QStringLiteral("canBack"), m_pages.canBack() },
        { QStringLiteral("sort"), current.sort }, { QStringLiteral("descending"), current.descending },
        { QStringLiteral("query"), m_pages.searchText() }, { QStringLiteral("route"), routeKey() },
        { QStringLiteral("artwork"), m_tracks.count() ? m_tracks.get(0).value(QStringLiteral("artwork")) : QVariant(QString()) } };
}
QString Client::routeKey() const { return m_pages.routeKey(); }
void Client::navigate(const QString& kind, const QString& key, const QString& title)
{
    m_pages.navigate(kind, key, title,
        [this](const QString& previous) { m_selection.remember(previous); },
        [this](const QString& next) { m_selection.restore(next); emit selectionChanged(); });
}
void Client::back()
{
    m_pages.back([this](const QString& previous) { m_selection.remember(previous); },
        [this](const QString& next) { m_selection.restore(next); emit selectionChanged(); },
        [this] { clearSelection(); });
}
void Client::search(const QString& text)
{
    m_pages.search(text, [this] { clearSelection(); });
}
void Client::refresh()
{
    m_pages.refreshSourcePath();
    rebuildNavigation();
    syncQueue(true);
    requestPage();
}
void Client::requestPage()
{
    m_pages.requestPage();
}
void Client::rebuildNavigation()
{
    m_navigation.replace(m_pages.rebuildNavigation());
}
void Client::syncTrackState()
{
    const auto id = m_player->currentTrack().id;
    const bool playing = m_player->state() == AudioEngine::State::Playing;
    if (m_lastPlayingId == id && m_lastPlaying == playing && m_lastFavorites == m_library->favoriteIds())
        return;
    m_tracks.setTrackState(m_library->favoriteIds(), id);
    m_lastPlayingId = id;
    m_lastPlaying = playing;
    m_lastFavorites = m_library->favoriteIds();
}
void Client::syncQueue(bool force)
{
    const auto entries = m_player->queue();
    const int index = m_player->index();
    if (!force && m_lastQueue == entries && m_lastQueueIndex == index)
        return;
    m_lastQueue = entries;
    m_lastQueueIndex = index;
    QVector<qint64> ids;
    for (const auto& e : entries)
        ids << e.trackId;
    const auto tracks = m_library->tracks(ids);
    QVector<QVariantMap> rows;
    const auto base = m_library->artwork()->filePath({});
    for (int i = 0; i < entries.size(); ++i) {
        auto row = trackRow(tracks.value(i), base);
        row["key"] = QString::number(entries[i].token);
        row["current"] = i == index;
        rows << row;
    }
    m_queue.replace(rows);
}
void Client::openCard(int row)
{
    const auto card = m_cards.get(row);
    if (!card.isEmpty())
        navigate(card.value("kind").toString(), card.value("key").toString(), card.value("title").toString());
}
void Client::playCard(int row, bool shuffle)
{
    openCard(row);
    m_pages.armPlay(routeKey(), shuffle);
}
void Client::playRow(int row)
{
    if (m_pages.busy() || row < 0 || row >= pageTracks().size())
        return;
    const auto track = pageTracks()[row];
    if (track.missing || !QFileInfo(track.path).isFile()) {
        showUnavailable(track.id);
        return;
    }
    if ((route().kind == "source" || route().kind == "playlist") && !m_player->replaceContextQueue())
        m_player->playSinglePreservingQueue(track.id);
    else {
        QVector<qint64> ids;
        for (const auto& t : pageTracks())
            ids << t.id;
        m_player->playTracks(ids, row, route().kind == "album");
    }
}
void Client::playAll(bool shuffle)
{
    if (m_pages.busy() || pageTracks().isEmpty())
        return;
    QVector<qint64> ids;
    for (const auto& t : pageTracks())
        ids << t.id;
    m_player->playTracks(ids, 0, route().kind == "album", shuffle);
}
void Client::sortBy(int column)
{
    m_pages.sortBy(column);
}
QString Client::rowKey(int row) const { return m_tracks.keyAt(row); }
bool Client::isSelected(int row) const { return m_selection.contains(rowKey(row)); }
void Client::selectRow(int row, int modifiers)
{
    if (m_selection.selectRow(row, m_tracks.count(), modifiers, [this](int index) { return rowKey(index); }))
        emit selectionChanged();
}
void Client::selectAll()
{
    m_selection.selectAll(m_tracks.count(), [this](int index) { return rowKey(index); });
    emit selectionChanged();
}
void Client::clearSelection()
{
    m_selection.clear();
    emit selectionChanged();
}
QVector<qint64> Client::selectedIds() const
{
    QVector<qint64> ids;
    for (int i = 0; i < pageTracks().size(); ++i)
        if (isSelected(i))
            ids << pageTracks()[i].id;
    return ids;
}
void Client::favoriteRow(int row)
{
    if (row >= 0 && row < pageTracks().size()) {
        const auto id = pageTracks()[row].id;
        m_library->setFavorites({ id }, !m_library->isFavorite(id));
    }
}
void Client::removeSelected()
{
    if (route().kind != "playlist")
        return;
    if (route().key.toLongLong() == m_library->favoritesPlaylistId())
        m_library->setFavorites(selectedIds(), false);
    else {
        QVector<int> positions;
        for (int i = 0; i < pageTracks().size(); ++i)
            if (isSelected(i))
                positions << pageTracks()[i].playlistPosition;
        m_library->removePlaylistPositions(route().key.toLongLong(), positions);
    }
}
void Client::moveTrack(int from, int to)
{
    if (route().kind != "playlist" || route().sort || route().key.toLongLong() == m_library->favoritesPlaylistId()
        || from < 0 || to < 0 || from >= pageTracks().size() || to >= pageTracks().size())
        return;
    m_library->movePlaylistItem(
        route().key.toLongLong(), pageTracks()[from].playlistPosition, pageTracks()[to].playlistPosition);
}
void Client::queueAction(const QString& a, int row, int to)
{
    if (a == "clear")
        m_player->clearUpcoming();
    else if (row >= 0 && row < m_queue.count()) {
        if (a == "play")
            m_player->activateQueueRow(row);
        else if (a == "remove")
            m_player->removeQueueRow(row);
        else if (a == "move" && to >= 0 && to < m_queue.count())
            m_player->moveQueue(row, to);
    }
}
void Client::setScroll(double offset) { m_pages.setScroll(offset); }
double Client::scroll() const { return m_pages.scroll(); }
void Client::resizeColumn(int column, double width) { m_pages.resizeColumn(column, width); }
void Client::setReduceMotion(bool value)
{
    if (m_reduceMotion == value)
        return;
    m_reduceMotion = value;
    m_library->setSetting(settings::reduceMotion, value ? QStringLiteral("1") : QStringLiteral("0"));
    emit reduceMotionChanged();
}
void Client::report(const QString& text)
{
    if (text == m_status)
        return;
    m_status = text;
    emit statusChanged();
}
QVariantMap Client::preferences() const
{
    return {{"replaceQueue", m_player->replaceContextQueue()}, {"normalize", m_player->normalizationEnabled()}, {"fading", m_player->fadingEnabled()},
        {"spatial", m_player->spatialEnabled()}, {"spatialStatus", m_player->spatialStatus()},
        {"exclusive", m_player->exclusive()}, {"replayGain", m_player->replayGainEnabled()}, {"reduceMotion", m_reduceMotion},
        {"dynamicPlaybackBackground", m_library->setting(settings::dynamicPlaybackBackground, QStringLiteral("0")) == QLatin1String("1")}};
}
void Client::setPreference(const QString& key, bool value)
{
    if (key == "replaceQueue") m_player->setReplaceContextQueue(value);
    else if (key == "fading") m_player->setFadingEnabled(value);
    else if (key == "normalize") m_player->setNormalizationEnabled(value);
    else if (key == "spatial") m_player->setSpatialEnabled(value);
    else if (key == "exclusive") m_player->setExclusive(value);
    else if (key == "replayGain") m_player->setReplayGainEnabled(value);
    else if (key == "reduceMotion") setReduceMotion(value);
    else if (key == "dynamicPlaybackBackground") m_library->setSetting(settings::dynamicPlaybackBackground, value ? QStringLiteral("1") : QStringLiteral("0"));
    emit preferencesChanged();
}
void Client::requestDialog(QVariantMap data, DialogService::Reply reply)
{
    m_dialogs.request(std::move(data), std::move(reply));
}
void Client::answerDialog(int token, const QVariantMap& answer)
{
    m_dialogs.answer(token, answer);
}
void Client::askName(const QString& title, const QString& initial, const QString& accept, std::function<void(QString)> reply)
{
    m_dialogs.askName(title, initial, accept, std::move(reply));
}
void Client::choosePath(const QString& title, const QString& mode, const QStringList& filters, std::function<void(QString)> reply)
{
    m_dialogs.choosePath(title, mode, filters, std::move(reply));
}
void Client::triggerMenu(int token)
{
    m_menus.trigger(token);
}
QPointF Client::cursorPosition() const { return m_system.cursorPosition(); }
void Client::popup(UiMenu& menu)
{
    m_menus.beginPopup();
    emit menuRequested(m_menus.describe(menu));
}
void Client::setMiniWindow(QWindow* window) { m_system.setMiniWindow(window); }
void Client::showMainWindow() { m_system.showMainWindow(); }
void Client::showSettings() { showMainWindow(); emit settingsRequested(); }
void Client::showMini() { m_system.showMini(); }
void Client::openUrls(const QVariantList& urls)
{
    QStringList paths;
    for (const auto& value : urls) { const QUrl url(value.toString()); if (url.isLocalFile()) paths << url.toLocalFile(); }
    openPaths(paths);
}
void Client::openPaths(const QStringList& paths)
{
    QVector<qint64> ids;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) m_library->addRoot(info.absoluteFilePath());
        else ids += m_library->importFiles({info.absoluteFilePath()});
    }
    if (!ids.isEmpty()) m_player->playTracks(ids, 0, false);
}
void Client::addMusicFolder()
{
    choosePath(QStringLiteral("添加音乐文件夹"), "folder", {}, [this](const QString& path) {
        const auto id = m_library->addRoot(path);
        if (!id) { report(QStringLiteral("无法登记文件夹，请检查目录和权限")); return; }
        navigate("source", QString::number(id), QFileInfo(path).fileName());
    });
}
void Client::createPlaylist(qint64 folder)
{
    askName(QStringLiteral("新建歌单"), QStringLiteral("新歌单"), QStringLiteral("创建"), [this, folder](const QString& name) {
        if (name.isEmpty()) return;
        const auto id = m_library->createPlaylist(name, folder);
        if (id) navigate("playlist", QString::number(id), name);
    });
}
void Client::createFolder()
{
    askName(QStringLiteral("新建歌单文件夹"), QStringLiteral("新文件夹"), QStringLiteral("创建"), [this](const QString& name) {
        if (!name.isEmpty()) m_library->createPlaylistFolder(name);
    });
}
void Client::importPlaylist()
{
    const auto target = route().kind == "playlist" ? route().key.toLongLong() : 0;
    choosePath(QStringLiteral("导入播放列表"), "open", {"播放列表 (*.m3u *.m3u8)"}, [this, target](const QString& path) {
        auto id = target;
        if (id <= 0 || id == m_library->favoritesPlaylistId()) id = m_library->createPlaylist(QFileInfo(path).completeBaseName());
        QString error;
        if (!m_library->importM3u(id, path, &error)) { report(error); return; }
        navigate("playlist", QString::number(id), QFileInfo(path).completeBaseName()); refresh();
    });
}
void Client::exportPlaylist()
{
    if (route().kind != "playlist") return;
    const auto target = route().key.toLongLong();
    choosePath(QStringLiteral("导出播放列表"), "save", {"播放列表 (*.m3u8)"}, [this, target](QString path) {
        if (QFileInfo(path).suffix().isEmpty()) path += ".m3u8";
        QString error; if (!m_library->exportM3u(target, path, &error)) report(error);
    });
}
void Client::showInfo(qint64 id)
{
    const auto track = m_library->track(id); if (!track.id) return;
    requestDialog({{"kind", "track"}, {"title", QStringLiteral("歌曲信息")}, {"trackTitle", track.title},
        {"artist", track.artist}, {"album", track.album}, {"albumArtist", track.albumArtist},
        {"trackNumber", track.trackNumber}, {"year", track.year}, {"cue", !track.cuePath.isEmpty()},
        {"artwork", artUrl(m_library->artwork()->filePath({}), track.artworkHash)}, {"path", track.path}},
        [this, id, cue = !track.cuePath.isEmpty()](const QVariantMap& a) {
            if (!a.value("accepted").toBool()) return;
            TagEdit edit; edit.title = a.value("title").toString(); edit.artist = a.value("artist").toString();
            edit.album = a.value("album").toString(); edit.albumArtist = a.value("albumArtist").toString();
            edit.trackNumber = std::clamp(a.value("trackNumber").toInt(), 0, 999);
            edit.year = std::clamp(a.value("year").toInt(), 0, 9999);
            const QUrl art(a.value("artFile").toString());
            if (!cue && art.isLocalFile()) {
                QFile file(art.toLocalFile()); QImageReader reader(art.toLocalFile());
                if (!file.open(QIODevice::ReadOnly) || !reader.canRead()) { report(QStringLiteral("无法读取所选封面")); return; }
                edit.editArtwork = true; edit.artwork = file.readAll();
                edit.artworkMime = reader.format().toLower() == "png" ? "image/png" : "image/jpeg";
            }
            QString error; if (!m_library->saveTags(id, edit, &error)) report(error);
        });
}
void Client::locateTrack(qint64 id)
{
    choosePath(QStringLiteral("重新定位音乐文件"), "open", {}, [this, id](const QString& path) {
        bool registered = false;
        for (const auto& root : m_library->roots()) if (root.path == QFileInfo(path).absolutePath()) registered = true;
        requestDialog({{"kind", "confirm"}, {"title", QStringLiteral("确认歌曲位置")},
            {"text", path + u'\n' + (registered ? QStringLiteral("属于已添加的文件夹，将进入全局资料库。")
                : QStringLiteral("文件夹未添加，只保留在队列、收藏或歌单中。"))}, {"accept", QStringLiteral("确认")}},
            [this, id, path](const QVariantMap& a) { if (a.value("accepted").toBool()) {
                QString error; if (!m_library->relocateTrack(id, path, &error)) report(error);
            }});
    });
}
void Client::showUnavailable(qint64 id)
{
    m_library->recheckTrack(id); const auto track = m_library->track(id);
    requestDialog({{"kind", "unavailable"}, {"title", QStringLiteral("歌曲暂时无法播放")},
        {"text", track.displayTitle() + u'\n' + track.path + QStringLiteral("\n文件可能已移动、磁盘未连接或访问权限已改变。")}},
        [this, id](const QVariantMap& a) {
            if (a.value("choice") == "check") m_library->recheckTrack(id);
            else if (a.value("choice") == "locate") locateTrack(id);
        });
}
void Client::trashTracks(const QVector<qint64>& ids)
{
    QStringList paths, details; int affected = 0;
    for (const auto& t : m_library->tracks(ids)) if (t.id && !t.path.isEmpty() && !paths.contains(t.path)) paths << t.path;
    for (const auto& path : paths) { details << path; for (const auto& t : m_library->tracksAtPath(path)) { details << "    " + t.displayTitle(); ++affected; } }
    if (paths.isEmpty()) return;
    requestDialog({{"kind", "confirm"}, {"title", QStringLiteral("移到废纸篓")}, {"destructive", true},
        {"text", QStringLiteral("将 %1 个音乐文件移到废纸篓？\n会影响 %2 首曲目，包括同一音频文件中的其他 CUE 曲目。引用保留为不可用，封面、歌词和 CUE 文件会保留。").arg(paths.size()).arg(affected)},
        {"details", details.join(u'\n')}, {"accept", QStringLiteral("移到废纸篓")}},
        [this, paths](const QVariantMap& a) { if (a.value("accepted").toBool()) m_player->trashFiles(paths); });
}
void Client::addToPlaylist(const QVector<qint64>& ids)
{
    UiMenu menu;
    for (const auto& p : m_library->playlists()) menu.addAction(p.name, this, [this, p, ids] { m_library->addToPlaylist(p.id, ids); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("新建歌单…"), this, [this, ids] {
        askName(QStringLiteral("新建歌单"), QStringLiteral("新歌单"), QStringLiteral("创建"), [this, ids](const QString& name) {
            if (!name.isEmpty()) m_library->addToPlaylist(m_library->createPlaylist(name), ids);
        });
    }); popup(menu);
}
void Client::trackMenu(int row)
{
    if (row < 0 || row >= pageTracks().size()) return;
    if (!isSelected(row)) selectRow(row);
    const auto ids = selectedIds(); const auto track = pageTracks()[row];
    UiMenu menu;
    menu.addAction(QStringLiteral("播放"), this, [this, row] { playRow(row); });
    menu.addAction(QStringLiteral("下一首播放"), this, [this, ids] { m_player->enqueueNext(ids); });
    menu.addAction(QStringLiteral("加入播放队列"), this, [this, ids] { m_player->enqueue(ids); });
    menu.addAction(QStringLiteral("添加到播放列表…"), this, [this, ids] { addToPlaylist(ids); });
    menu.addAction(m_library->isFavorite(track.id) ? QStringLiteral("取消喜欢") : QStringLiteral("添加到我喜欢的"),
        this, [this, ids, liked = !m_library->isFavorite(track.id)] { m_library->setFavorites(ids, liked); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("歌曲信息…"), this, [this, track] { showInfo(track.id); });
    menu.addAction(QStringLiteral("在文件管理器中显示"), this, [track] { showInFileManager(track.path); });
    if (track.missing) {
        menu.addAction(QStringLiteral("重新检查"), this, [this, track] { m_library->recheckTrack(track.id); });
        menu.addAction(QStringLiteral("重新定位…"), this, [this, track] { locateTrack(track.id); });
    }
    if (route().kind == "playlist") {
        menu.addSeparator();
        const auto playlistId = route().key.toLongLong();
        menu.addAction(playlistId == m_library->favoritesPlaylistId() ? QStringLiteral("从我喜欢的移除") : QStringLiteral("从播放列表移除"), this, &Client::removeSelected);
        if (!route().sort && playlistId != m_library->favoritesPlaylistId()) {
            menu.addAction(QStringLiteral("上移"), this, [this, row] { moveTrack(row, row - 1); });
            menu.addAction(QStringLiteral("下移"), this, [this, row] { moveTrack(row, row + 1); });
        }
    }
    menu.addSeparator(); menu.addAction(QStringLiteral("移到废纸篓…"), this, [this, ids] { trashTracks(ids); }); popup(menu);
}
void Client::cardMenu(int row)
{
    const auto card = m_cards.get(row); const auto kind = card.value("kind").toString();
    if (kind == "source") { sourceMenu(card.value("key").toLongLong()); return; }
    if (kind == "playlist") { playlistMenu(card.value("key").toLongLong()); return; }
    UiMenu menu; menu.addAction(QStringLiteral("播放全部"), this, [this, row] { playCard(row, false); });
    menu.addAction(QStringLiteral("随机播放"), this, [this, row] { playCard(row, true); }); popup(menu);
}
void Client::navigationMenu(int row)
{
    const auto item = m_navigation.get(row); if (item.value("protected").toBool()) return;
    if (item.value("kind") == "playlistSection") {
        UiMenu menu; menu.addAction(QStringLiteral("新建歌单…"), this, [this] { createPlaylist(); });
        menu.addAction(QStringLiteral("新建歌单文件夹…"), this, &Client::createFolder);
        menu.addAction(QStringLiteral("导入播放列表…"), this, &Client::importPlaylist); popup(menu);
    } else if (item.value("kind") == "playlist") playlistMenu(item.value("key").toLongLong());
    else if (item.value("kind") == "listFolder") folderMenu(item.value("key").toLongLong());
}
void Client::sourceMenu(qint64 id)
{
    const auto root = m_library->root(id); UiMenu menu;
    menu.addAction(QStringLiteral("打开"), this, [this, root] { navigate("source", QString::number(root.id), root.name); });
    menu.addAction(QStringLiteral("创建播放列表快照…"), this, [this, root] {
        askName(QStringLiteral("创建播放列表"), root.name, QStringLiteral("创建"), [this, root](const QString& name) {
            if (name.isEmpty()) return; QVector<qint64> ids;
            for (const auto& t : m_library->folderTracks(root.id)) ids << t.id;
            const auto playlist = m_library->createPlaylistFromFolder(root.id, name, ids);
            if (playlist) navigate("playlist", QString::number(playlist), name);
        });
    });
    menu.addAction(QStringLiteral("重新扫描"), this, [this, id] { m_library->refreshRoot(id); });
    menu.addAction(QStringLiteral("在文件管理器中显示"), this, [root] { showInFileManager(root.path); });
    menu.addAction(QStringLiteral("重新定位文件夹…"), this, [this, id] {
        choosePath(QStringLiteral("重新定位文件夹"), "folder", {}, [this, id](const QString& path) {
            QString error; if (!m_library->relocateRoot(id, path, &error)) report(error);
        });
    });
    menu.addSeparator(); menu.addAction(QStringLiteral("取消登记来源"), this, [this, id] {
        m_library->removeRoot(id); if (route().kind == "source" && route().key.toLongLong() == id) navigate("sources", {}, QStringLiteral("文件夹"));
    }); popup(menu);
}
void Client::playlistMenu(qint64 id)
{
    Playlist current; for (const auto& p : m_library->playlists()) if (p.id == id) current = p;
    if (!current.id || current.isFavorites()) return;
    UiMenu menu; menu.addAction(QStringLiteral("重命名…"), this, [this, current] {
        askName(QStringLiteral("重命名歌单"), current.name, QStringLiteral("完成"), [this, current](const QString& name) {
            if (!name.isEmpty()) m_library->renamePlaylist(current.id, name);
        });
    });
    auto* move = menu.addMenu(QStringLiteral("移动到"));
    move->addAction(QStringLiteral("顶层"), this, [this, id] { m_library->movePlaylistToFolder(id, 0); });
    for (const auto& f : m_library->playlistFolders()) move->addAction(f.name, this, [this, id, f] { m_library->movePlaylistToFolder(id, f.id); });
    menu.addAction(QStringLiteral("导出播放列表…"), this, [this, current] { navigate("playlist", QString::number(current.id), current.name); exportPlaylist(); });
    menu.addAction(QStringLiteral("删除歌单"), this, [this, id] {
        m_library->deletePlaylist(id); if (route().kind == "playlist" && route().key.toLongLong() == id) navigate("songs", {}, QStringLiteral("歌曲"));
    }); popup(menu);
}
void Client::folderMenu(qint64 id)
{
    PlaylistFolder current; for (const auto& f : m_library->playlistFolders()) if (f.id == id) current = f;
    UiMenu menu; menu.addAction(QStringLiteral("在此新建歌单"), this, [this, id] { createPlaylist(id); });
    menu.addAction(QStringLiteral("重命名…"), this, [this, current] {
        askName(QStringLiteral("重命名文件夹"), current.name, QStringLiteral("完成"), [this, current](const QString& name) {
            if (!name.isEmpty()) m_library->renamePlaylistFolder(current.id, name);
        });
    });
    menu.addAction(QStringLiteral("删除歌单文件夹"), this, [this, id] { m_library->deletePlaylistFolder(id); }); popup(menu);
}
void Client::action(const QString& name)
{
    if (name == "addSource") addMusicFolder();
    else if (name == "newPlaylist") createPlaylist();
    else if (name == "newFolder") createFolder();
    else if (name == "import") importPlaylist();
    else if (name == "export") exportPlaylist();
    else if (name == "settings") showSettings();
    else if (name == "mini") showMini();
    else if (name == "undo") m_library->undo();
    else if (name == "redo") m_library->redo();
    else if (name == "quit") quitApplication();
    else if (name == "refresh") m_library->rescanAll();
    else if (name == "selectAll") selectAll();
    else if (name == "menu") popup(m_menus.bar());
    else if (name == "pageMenu") {
        if (route().kind == "source") sourceMenu(route().key.toLongLong());
        else if (route().kind == "playlist") playlistMenu(route().key.toLongLong());
    } else if (name == "trash") trashTracks(selectedIds());
}
void Client::setupMenus()
{
    auto* file = m_menus.bar().addMenu(QStringLiteral("文件"));
    file->addAction(QStringLiteral("添加音乐文件夹…"), this, &Client::addMusicFolder);
    file->addAction(QStringLiteral("导入播放列表…"), this, &Client::importPlaylist);
    file->addAction(QStringLiteral("导出播放列表…"), this, &Client::exportPlaylist);
    file->addAction(QStringLiteral("设置…"), this, &Client::showSettings);
    file->addAction(QStringLiteral("退出 Echo"), this, [] { quitApplication(); });
    auto* edit = m_menus.bar().addMenu(QStringLiteral("编辑"));
    edit->addAction(QStringLiteral("撤销"), m_library, &Library::undo);
    edit->addAction(QStringLiteral("重做"), m_library, &Library::redo);
    edit->addAction(QStringLiteral("新建歌单…"), this, [this] { createPlaylist(); });
    edit->addAction(QStringLiteral("新建歌单文件夹…"), this, &Client::createFolder);
    auto* control = m_menus.bar().addMenu(QStringLiteral("控制"));
    control->addAction(QStringLiteral("播放/暂停"), m_player, &Player::toggle);
    control->addAction(QStringLiteral("下一首"), m_player, &Player::next);
    control->addAction(QStringLiteral("上一首"), m_player, &Player::previous);
    control->addAction(QStringLiteral("迷你播放器"), this, &Client::showMini);
    auto* view = m_menus.bar().addMenu(QStringLiteral("显示"));
    view->addAction(QStringLiteral("设置…"), this, &Client::showSettings);
}
}
