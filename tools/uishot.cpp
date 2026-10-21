#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"
#include "ui/playerbar.h"
#include "ui/miniplayer.h"
#include "ui/settingsdialog.h"
#include "ui/pages.h"
#include <QToolButton>
#include <QAccessible>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QTreeWidget>
#include <QScrollBar>
#include <QMouseEvent>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QSlider>
#include <QDebug>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QTimer>

#ifdef Q_OS_MACOS
void verifyNativeMini(QWidget* widget);
#endif

namespace {

void writeTone(const QString& path, const QString& title, const QString& artist, const QString& album, int track)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("sine=frequency=440:duration=1:sample_rate=44100"), QStringLiteral("-c:a"),
                  QStringLiteral("flac"), QStringLiteral("-metadata"), QStringLiteral("title=") + title,
                  QStringLiteral("-metadata"), QStringLiteral("artist=") + artist, QStringLiteral("-metadata"),
                  QStringLiteral("album=") + album, QStringLiteral("-metadata"), QStringLiteral("track=") + QString::number(track),
                  path});
    ffmpeg.waitForFinished(20000);
}

void grab(QWidget* window, const QString& directory, const QString& name)
{
    QApplication::processEvents();
    window->grab().save(directory + QLatin1Char('/') + name + QStringLiteral(".png"));
}

} // namespace

int main(int argc, char** argv)
{
#ifdef ECHO_QT_PLUGINS
    QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    echo::useDarkTheme();
    echo::useDarkAppearance();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("EchoFavoritesShot"));
    QCoreApplication::setApplicationName(QStringLiteral("EchoFavoritesShot"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QFont font(QStringLiteral(".AppleSystemUIFont"), 13);
    application.setFont(font);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/echo-shots");
    QDir().mkpath(out);

    const QString root = QDir::temp().filePath(QStringLiteral("echo-shot-lib"));
    QDir(root).removeRecursively();
    writeTone(root + QStringLiteral("/Aria/Harbor/01.flac"), QStringLiteral("夜航"), QStringLiteral("Aria"), QStringLiteral("港湾"), 1);
    writeTone(root + QStringLiteral("/Aria/Harbor/02.flac"), QStringLiteral("清晨"), QStringLiteral("Aria"), QStringLiteral("港湾"), 2);
    writeTone(root + QStringLiteral("/Aria/Letters/01.flac"), QStringLiteral("纸页"), QStringLiteral("Aria"), QStringLiteral("来信"), 1);
    writeTone(root + QStringLiteral("/Mio/Glass/01.flac"), QStringLiteral("窗"), QStringLiteral("Mio"), QStringLiteral("玻璃"), 1);

    echo::Library library;
    QString error;
    if (!library.open(&error)) {
        return 1;
    }
    QEventLoop loop;
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    QObject::connect(&library,&echo::Library::scanFinished,&loop,[&] { if(!library.scanBusy()) loop.quit(); });
    for (const auto& folder:QStringList {"/Aria/Harbor","/Aria/Letters","/Mio/Glass"}) library.addRoot(root+folder);
    loop.exec();

    echo::AudioEngine engine;
    echo::Player player(&library, &engine);
    player.setVolume(0.f); // Exercise the real output backend without emitting a test tone.
    echo::MainWindow window(&library, &player, &engine);
    window.resize(1180, 760);
    window.show();
    QApplication::processEvents();

    auto* nav = window.findChild<QListWidget*>(QStringLiteral("libraryNav"));
    if (nav) {
        nav->setCurrentRow(0);
    }
    grab(&window, out, QStringLiteral("songs"));
    if (nav) {
        nav->setCurrentRow(1);
    }
    grab(&window, out, QStringLiteral("albums"));
    auto* grid = window.findChild<QListWidget*>(QStringLiteral("albumGrid"));
    if (grid && grid->count() > 0) {
        emit grid->itemClicked(grid->item(0));
        grab(&window, out, QStringLiteral("album"));
    }
    if (nav) {
        nav->setCurrentRow(2);
    }
    grab(&window, out, QStringLiteral("artists"));
    if (nav) {
        nav->setCurrentRow(3);
    }
    grab(&window, out, QStringLiteral("folders"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("search"));
    if (search) {
        search->setText(QStringLiteral("Aria"));
        grab(&window, out, QStringLiteral("search"));
        search->clear();
    }
    if (nav) {
        nav->setCurrentRow(0);
    }
    auto check = [](bool ok, const char* message) { if (!ok) qFatal("%s", message); };
    const auto tracks = library.allTracks(QStringLiteral("title"), false);
    check(!tracks.isEmpty(), "fixture tracks imported");
    auto* bar = window.findChild<echo::PlayerBar*>();
    auto* mini = window.findChild<echo::MiniPlayer*>();
    auto* barHeart = bar->findChild<QToolButton*>(QStringLiteral("favoriteButton"));
    auto* miniHeart = mini->findChild<QToolButton*>(QStringLiteral("favoriteButton"));
    check(barHeart && miniHeart && !barHeart->isEnabled() && !miniHeart->isEnabled(), "no track disables both hearts");
    auto* songsTable = window.findChild<echo::SongsPage*>()->table();
    auto* playlistPage = window.findChild<echo::PlaylistPage*>();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("playlistTree"));
    check(tree && tree->topLevelItem(0)->data(0, Qt::UserRole).toLongLong() == library.favoritesPlaylistId(), "favorite pinned first");
    emit tree->itemClicked(tree->topLevelItem(0), 0);
    auto* emptyFavorites = window.findChild<QLabel*>(QStringLiteral("emptyFavorites"));
    check(emptyFavorites->isVisible() && !playlistPage->table()->isVisible(), "favorite empty guidance");
    nav->setCurrentRow(0);
    // Restore a current track without starting playback, just as on application launch.
    QJsonArray favoriteQueue;
    for (const auto& track : tracks) favoriteQueue.append(track.id);
    library.setSetting(QStringLiteral("queue"), QString::fromUtf8(QJsonDocument(favoriteQueue).toJson(QJsonDocument::Compact)));
    library.setSetting(QStringLiteral("queue_index"), QStringLiteral("0"));
    player.restoreSession();
    const qint64 firstId = player.currentTrack().id;
    check(firstId > 0 && barHeart->isEnabled() && miniHeart->isEnabled(), "restored current track supports favorite");
    int broadRefreshes = 0;
    int songResets = 0;
    QObject::connect(&library, &echo::Library::changed, &window, [&] { ++broadRefreshes; });
    QObject::connect(songsTable->model(), &QAbstractItemModel::modelReset, &window, [&] { ++songResets; });
    auto click = [](QWidget* widget, QPoint position) {
        QMouseEvent press(QEvent::MouseButtonPress, position, widget->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position, widget->mapToGlobal(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &release);
    };
    click(barHeart, barHeart->rect().center());
    check(library.isFavorite(firstId) && barHeart->isChecked() && miniHeart->isChecked(), "main heart synchronizes mini");
    check(broadRefreshes == 0 && songResets == 0, "favorite avoids broad refresh and song reset");
    auto* favoriteAction = QAccessible::queryAccessibleInterface(barHeart)->actionInterface();
    check(favoriteAction != nullptr, "favorite accessibility action exists");
    favoriteAction->doAction(QAccessibleActionInterface::toggleAction());
    check(!library.isFavorite(firstId) && !barHeart->isChecked() && !miniHeart->isChecked(), "accessible toggle commits unlike");
    favoriteAction->doAction(QAccessibleActionInterface::toggleAction());
    check(library.isFavorite(firstId) && barHeart->isChecked() && miniHeart->isChecked(), "accessible toggle commits favorite");
    library.undo();
    check(!library.isFavorite(firstId) && !barHeart->isChecked(), "favorite undo");
    library.redo();
    check(library.isFavorite(firstId) && barHeart->isChecked(), "favorite redo");
    const qint64 secondId = tracks[1].id;
    library.setFavorites({secondId}, true);
    const QVector<qint64> orderedFavorites {secondId, firstId};
    check(library.playlistTrackIds(library.favoritesPlaylistId()) == orderedFavorites, "favorite recency ordering");
    library.undo();
    library.redo();
    check(library.playlistTrackIds(library.favoritesPlaylistId()) == orderedFavorites, "redo restores order");
    const int historyRefreshes = broadRefreshes;
    library.renamePlaylist(library.favoritesPlaylistId(), QStringLiteral("bad"));
    library.deletePlaylist(library.favoritesPlaylistId());
    library.movePlaylistToFolder(library.favoritesPlaylistId(), 123);
    library.movePlaylistItem(library.favoritesPlaylistId(), 0, 1);
    check(broadRefreshes == historyRefreshes && library.playlistTrackIds(library.favoritesPlaylistId()) == orderedFavorites,
          "service protects system metadata and ordering without undo pollution");
    library.undo();
    check(!library.isFavorite(secondId), "blocked operations do not consume undo");
    library.redo();
    const int songFirst = songsTable->trackModel()->ids().indexOf(firstId);
    songsTable->selectionModel()->select(songsTable->model()->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    click(songsTable->viewport(), songsTable->favoriteRect(songFirst).center());
    check(!library.isFavorite(firstId) && !barHeart->isChecked() && songsTable->selectionModel()->selectedRows().size() == 1,
          "row heart synchronizes player without selection change");
    mini->show();
    QApplication::processEvents();
    click(miniHeart, miniHeart->rect().center());
    check(library.isFavorite(firstId) && barHeart->isChecked() && miniHeart->isChecked(), "mini heart synchronizes main");
    grab(mini, out, QStringLiteral("favorites-mini"));
    emit mini->expandRequested();
    // Check every shared table consumes the same state, even before its page is opened.
    for (auto* table : window.findChildren<echo::TrackTable*>()) {
        table->setTracks(tracks);
        const int row = table->trackModel()->ids().indexOf(firstId);
        check(table->trackModel()->index(row, 1).data(Qt::UserRole + 4).toBool(), "all table models reflect favorite");
    }
    emit tree->itemClicked(tree->topLevelItem(0), 0);
    playlistPage->reload();
    check(playlistPage->table()->trackModel()->ids() == QVector<qint64>({firstId, secondId}) && !emptyFavorites->isVisible(), "favorite page updates members");
    tree->setCurrentItem(tree->topLevelItem(0));
    const QPoint favoriteHover = tree->visualItemRect(tree->topLevelItem(0)).center();
    QMouseEvent hover(QEvent::MouseMove, favoriteHover, tree->viewport()->mapToGlobal(favoriteHover),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &hover);
    grab(&window, out, QStringLiteral("favorites"));
    playlistPage->table()->selectionModel()->select(playlistPage->table()->model()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    click(playlistPage->table()->viewport(), playlistPage->table()->favoriteRect(0).center());
    check(!library.isFavorite(firstId) && playlistPage->table()->trackModel()->ids() == QVector<qint64>{secondId}
          && playlistPage->table()->selectionModel()->isRowSelected(0, {}), "favorite removal preserves remaining selection");
    emit playlistPage->table()->removeRequested({secondId});
    check(library.favoriteIds().isEmpty() && emptyFavorites->isVisible() && library.track(secondId).id > 0, "batch unfavorite keeps library track");
    library.undo();
    check(library.isFavorite(secondId), "batch unlike undo");
    // Failed mutations must leave the state and undo history intact.
    echo::Database failureDb;
    check(failureDb.open(library.databasePath(), QStringLiteral("favorite-failure"), &error), "failure test connection");
    QSqlQuery sql(QSqlDatabase::database(QStringLiteral("favorite-failure")));
    check(sql.exec(QStringLiteral("CREATE TRIGGER reject_favorite BEFORE DELETE ON playlist_items WHEN old.playlist_id=%1 BEGIN SELECT RAISE(ABORT, 'injected failure'); END").arg(library.favoritesPlaylistId())), "failure trigger");
    // Exercise the service without opening its real error dialog in the regression harness.
    QObject::disconnect(&library, &echo::Library::favoritesFailed, &window, nullptr);
    int favoriteErrors = 0;
    QObject::connect(&library, &echo::Library::favoritesFailed, &window, [&](const QString&) { ++favoriteErrors; });
    check(!library.setFavorites({firstId}, true) && !library.isFavorite(firstId) && !barHeart->isChecked() && favoriteErrors == 1,
          "failed write leaves hearts unchanged");
    library.redo(); // Redo the prior batch removal; rollback must retain the favorite.
    check(library.isFavorite(secondId) && library.canRedo() && favoriteErrors == 2, "failed redo keeps state and history");
    sql.exec(QStringLiteral("DROP TRIGGER reject_favorite"));
    library.redo();
    check(!library.isFavorite(secondId), "failed redo can be retried");
    library.setFavorites({firstId}, true);
    library.addToPlaylist(library.favoritesPlaylistId(), {firstId, firstId});
    check(library.playlistTrackIds(library.favoritesPlaylistId()) == QVector<qint64>{firstId}, "add-to-playlist favorite dedup");
    QString exported = out + QStringLiteral("/favorites.m3u8");
    check(library.exportM3u(library.favoritesPlaylistId(), exported, &error), "favorite export");
    check(!library.importM3u(library.favoritesPlaylistId(), exported, &error), "direct import cannot mutate favorite");
    const auto imported = library.createPlaylist(QStringLiteral("imported"));
    check(library.importM3u(imported, exported, &error) && library.playlistTrackIds(imported) == QVector<qint64>{firstId}, "ordinary import retains behavior");
    emit tree->itemClicked(tree->topLevelItem(0), 0);
    QTimer::singleShot(150, &window, [&] {
        auto* dialog = window.findChild<QFileDialog*>();
        check(dialog != nullptr, "import menu opens file picker");
        auto* filename = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        check(filename != nullptr, "file picker filename field");
        filename->setText(exported);
        check(QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection), "accept file picker");
    });
    QTimer importDeadline;
    importDeadline.setSingleShot(true);
    QObject::connect(&importDeadline, &QTimer::timeout, &window, [] { qFatal("import file picker timed out"); });
    importDeadline.start(5000);
    for (auto* action : window.menuBar()->actions().at(0)->menu()->actions()) {
        if (action->text() == QStringLiteral("导入播放列表…")) action->trigger();
    }
    importDeadline.stop();
    check(playlistPage->playlistId() != library.favoritesPlaylistId()
          && library.playlistTrackIds(playlistPage->playlistId()) == QVector<qint64>{firstId}
          && library.playlistTrackIds(library.favoritesPlaylistId()) == QVector<qint64>{firstId},
          "import menu while viewing favorite creates ordinary playlist");
    bool importedHeading = false;
    for (auto* label : window.findChildren<QLabel*>())
        importedHeading |= label->isVisible() && label->text() == QStringLiteral("favorites");
    check(importedHeading, "import updates visible playlist heading");
    // Unlike during actual playback must preserve the current queue and playback.
    player.setSpatialEnabled(false);
    player.setExclusive(false);
    player.playTracks({firstId, secondId}, 0, false);
    QEventLoop playbackLoop;
    QTimer::singleShot(250, &playbackLoop, &QEventLoop::quit);
    playbackLoop.exec();
    check(player.state() == echo::AudioEngine::State::Playing, "favorite test starts real playback");
    const auto queueBefore = player.queue();
    const qint64 positionBefore = player.positionMs();
    click(barHeart, barHeart->rect().center());
    check(!library.isFavorite(firstId) && player.state() == echo::AudioEngine::State::Playing
          && player.currentTrack().id == firstId && player.queue().size() == queueBefore.size()
          && player.queue()[0].token == queueBefore[0].token && player.positionMs() >= positionBefore,
          "unfavorite during playback preserves playback and queue tokens");
    auto waitState = [&](echo::AudioEngine::State state) {
        if (engine.state() == state) return;
        QEventLoop stateLoop;
        QTimer::singleShot(2000, &stateLoop, &QEventLoop::quit);
        QObject::connect(&engine, &echo::AudioEngine::stateChanged, &stateLoop, [&] {
            if (engine.state() == state) stateLoop.quit();
        });
        stateLoop.exec();
        check(engine.state() == state, "engine reaches requested state");
    };
    engine.pause();
    waitState(echo::AudioEngine::State::Paused);
    click(miniHeart, miniHeart->rect().center());
    check(library.isFavorite(firstId) && player.state() == echo::AudioEngine::State::Paused, "favorite while paused");
    engine.stop();
    waitState(echo::AudioEngine::State::Stopped);
    nav->setCurrentRow(0);
    grab(&window, out, QStringLiteral("favorites-songs"));
    qInfo() << "Favorites window, player, history, failure and playback integration checks passed";

    bar->setTrack(tracks.front(), {});
    bar->setFormatText(QStringLiteral("44.1 kHz · 32-bit float → 48.0 kHz"));
    bar->setPlaying(true);
    bar->setPosition(8000, 143000);
    for (auto* label : bar->findChildren<QLabel*>()) {
        if (label->toolTip() != tracks.front().displayTitle()) continue;
        const QRect titleBounds(label->mapTo(bar, QPoint()), label->size());
        const QRect heartBounds(barHeart->mapTo(bar, QPoint()), barHeart->size());
        check(label->text() == tracks.front().displayTitle(), "short player title remains complete");
        check(heartBounds.left() >= titleBounds.right() + 6 && heartBounds.left() <= titleBounds.right() + 10,
              "player heart follows title with a small consistent gap");
    }
    grab(&window, out, QStringLiteral("playing"));
    auto* format = bar->findChild<QLabel*>(QStringLiteral("audioFormat"));
    auto* seek = bar->findChild<QSlider*>(QStringLiteral("playbackSeek"));
    check(std::abs(format->mapTo(bar, format->rect().center()).y() - seek->mapTo(bar, seek->rect().center()).y()) <= 1,
          "format and progress share a horizontal centerline");
    window.resize(1040, 640);
    grab(&window, out, QStringLiteral("minimum"));
    check(bar->width() <= window.centralWidget()->width() - 260, "player fits minimum width");
    check(seek->width() >= 120, "seek remains usable at minimum width");
    auto longTitleTrack = tracks.front();
    longTitleTrack.title = QStringLiteral("很长的歌曲名称 A Very Long Song Title With Extra Information");
    bar->setTrack(longTitleTrack, {});
    QApplication::processEvents();
    for (auto* label : bar->findChildren<QLabel*>()) {
        if (label->toolTip() != longTitleTrack.displayTitle()) continue;
        const QRect titleBounds(label->mapTo(bar, QPoint()), label->size());
        const QRect heartBounds(barHeart->mapTo(bar, QPoint()), barHeart->size());
        check(label->text().endsWith(QChar(0x2026)) && heartBounds.left() >= titleBounds.right() + 6,
              "long player title elides without overlapping favorite at minimum width");
    }
    grab(&window, out, QStringLiteral("long-title"));
    bar->setTrack(tracks.front(), {});

    window.resize(1180, 760);
    window.close();
    QApplication::processEvents();
    check(!window.isVisible() && !application.quitOnLastWindowClosed(), "close hides without quitting");
    window.showMainWindow();
    check(window.isVisible(), "reopen hidden main window");
    window.showSettings();
    auto* settings = window.findChild<echo::SettingsDialog*>();
    auto* toggle = settings->findChild<QCheckBox*>(QStringLiteral("normalizationToggle"));
    QJsonArray ids;
    for (const auto& track : tracks) ids.append(track.id);
    ids.append(tracks.front().id);
    library.setSetting(QStringLiteral("queue"), QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));
    player.restoreSession();
    toggle->setChecked(true);
    check(player.normalizationEnabled(), "settings enables normalization");
    check(library.setting(QStringLiteral("normalization")) == QLatin1String("1"), "normalization persists immediately");
    QEventLoop analysisLoop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &analysisLoop, &QEventLoop::quit);
    QObject::connect(&player, &echo::Player::normalizationChanged, &analysisLoop, [&] {
        if (player.normalizationStatus().contains(QStringLiteral("已完成"))) analysisLoop.quit();
    });
    deadline.start(15000);
    analysisLoop.exec();
    check(player.normalizationStatus().contains(QStringLiteral("已完成")), "all untagged queue tracks analyzed");
    check(player.normalizationStatus().contains(QStringLiteral("4 首")), "duplicate tracks share analysis");
    check(player.state() == echo::AudioEngine::State::Stopped, "normalizing restored queue does not start playback");
#ifdef Q_OS_MACOS
    auto* spatial = settings->findChild<QCheckBox*>(QStringLiteral("spatialAudioToggle"));
    check(spatial != nullptr, "macOS settings offers spatial audio");
    player.setExclusive(true);
    spatial->setChecked(true);
    check(player.spatialEnabled() && !player.exclusive(), "spatial audio disables exclusive output");
    check(library.setting(QStringLiteral("spatial_audio")) == QLatin1String("1"), "spatial preference persists");
    player.setExclusive(true);
    check(!player.spatialEnabled() && !spatial->isChecked(), "exclusive output disables spatial setting");
    player.setExclusive(false);
    spatial->setChecked(true);
#endif
    grab(settings, out, QStringLiteral("settings"));
#ifdef Q_OS_MACOS
    spatial->setChecked(false);
#endif
    settings->hide();
    mini->show();
    QApplication::processEvents();
    mini->setTrack(tracks.front(), {}, true);
    window.hide();
    check(mini->windowFlags().testFlag(Qt::FramelessWindowHint), "mini is frameless");
    check(mini->windowFlags().testFlag(Qt::WindowStaysOnTopHint), "mini stays on top");
    grab(mini, out, QStringLiteral("mini"));
#ifdef Q_OS_MACOS
    verifyNativeMini(mini);
#endif
    emit mini->expandRequested();
    check(window.isVisible() && !mini->isVisible(), "mini expands into one main window");
    player.setNormalizationEnabled(false);
    qInfo() << "Window lifecycle, layout, settings and playlist normalization checks passed";
    const qint64 fixtureFolder = library.createPlaylistFolder(QStringLiteral("歌单文件夹"));
    library.createPlaylist(QStringLiteral("文件夹内歌单"), fixtureFolder);
    QApplication::processEvents();
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
        auto* folderItem = tree->topLevelItem(row);
        if (folderItem->data(0, Qt::UserRole + 1).toString() != QLatin1String("folder")) continue;
        const QPoint disclosure(6, tree->visualItemRect(folderItem).center().y());
        click(tree->viewport(), disclosure);
        check(!folderItem->isExpanded(), "aligned root folder disclosure collapses");
        click(tree->viewport(), disclosure);
        check(folderItem->isExpanded(), "aligned root folder disclosure expands");
    }
    grab(&window, out, QStringLiteral("playlist-folders"));
    if (qEnvironmentVariableIsSet("ECHO_UISHOT_HOLD")) {
        settings->hide();
        window.showMainWindow();
        nav->setCurrentRow(0);
        return application.exec();
    }
    return 0;
}
