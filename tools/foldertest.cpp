#include "ui/miniplayer.h"
#include "ui/playerbar.h"
#include <QSlider>
#include "ui/dialogs.h"
#include "ui/trackdialog.h"
#include <QDialogButtonBox>
#include <QTextEdit>
#include "library/library.h"
#include "library/scanner.h"
#include "library/database.h"
#include "playback/player.h"
#include "ui/mainwindow.h"
#include "ui/pages.h"
#include "ui/settingsdialog.h"
#include "ui/theme.h"
#include <QApplication>
#include <QCheckBox>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QInputDialog>
#include <QTimer>
#include <QEventLoop>
#include <QSqlQuery>
#include <ctime>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QUuid>
#include <atomic>
#include <cmath>
#include <thread>

namespace {
int checks = 0;
void check(bool ok, const char* message)
{
    if (!ok)
        qFatal("FAIL %s", message);
    ++checks;
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 8000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (predicate())
            return true;
        QThread::msleep(5);
    } while (timer.elapsed() < timeout);
    return false;
}
void settled(echo::Library& library)
{
    check(waitFor([&] { return !library.scanBusy(); }), "scan settles");
}
void wave(const QString& path, int milliseconds = 350)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "fixture open");
    const int frames = 48000 * milliseconds / 1000;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(frames * 2);
    for (int i = 0; i < frames; ++i)
        out << qint16(std::sin(i * 2 * 3.141592653589793 * 440 / 48000) * 5000);
}
class SilentOutput final : public echo::AudioOutput {
public:
    ~SilentOutput() override
    {
        close();
    }
    bool open(const echo::SampleFormat& source, bool, echo::DeviceFormat* opened, QString*) override
    {
        rate = source.sampleRate;
        channels = source.channels;
        *opened = { rate, channels, 32, 4, true };
        openedFlag = true;
        return true;
    }
    void close() override
    {
        pause();
        openedFlag = false;
    }
    void setPull(Pull value) override
    {
        pull = std::move(value);
    }
    void start() override
    {
        pause();
        running = true;
        worker = std::thread([this] {
            std::vector<float> block(rate / 200 * channels);
            while (running) {
                const int got = pull(reinterpret_cast<uint8_t*>(block.data()), rate / 200);
                for (int i = 0; i < got * channels; ++i)
                    if (std::abs(block[i]) > .01f)
                        ++nonzero;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    void pause() override
    {
        running = false;
        if (worker.joinable())
            worker.join();
    }
    bool isOpen() const override
    {
        return openedFlag;
    }
    std::atomic<int> nonzero { 0 };

private:
    Pull pull;
    int rate = 48000, channels = 1;
    bool openedFlag = false;
    std::atomic<bool> running { false };
    std::thread worker;
};
void click(QWidget* target, const QPoint& point)
{
    QMouseEvent down(
        QEvent::MouseButtonPress, point, target->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &down);
    QMouseEvent up(
        QEvent::MouseButtonRelease, point, target->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &up);
    QApplication::processEvents();
}
void menuAction(QWidget* owner, const QPoint& point, const QString& label, const QString& name = { },
    QWidget* signalTarget = nullptr)
{
    QTimer::singleShot(0, [owner, label, name] {
        QMenu* menu = nullptr;
        for (auto* widget : QApplication::allWidgets())
            if (auto* candidate = qobject_cast<QMenu*>(widget))
                if (candidate->parentWidget() == owner)
                    menu = candidate;
        check(menu, "context menu opened");
        QAction* action = nullptr;
        for (auto* item : menu->actions())
            if (item->text() == label)
                action = item;
        check(action, "context action available");
        if (label == QStringLiteral("移除文件夹…"))
            QTimer::singleShot(0, [] {
                auto* box = qobject_cast<echo::MessageDialog*>(QApplication::activeModalWidget());
                check(box && box->button(QMessageBox::Yes)->text() == QStringLiteral("移除文件夹"),
                    "actual source removal uses the shared Chinese confirmation");
                box->grab().save(QStringLiteral("/tmp/echo-dialog-source-removal.png"));
                box->button(QMessageBox::Cancel)->click();
            });
        if (!name.isEmpty())
            QTimer::singleShot(0, [name] {
                auto* input = qobject_cast<echo::NameDialog*>(QApplication::activeModalWidget());
                check(input, "playlist naming dialog opened");
                input->setTextValue(name);
                input->accept();
            });
        action->trigger();
        menu->close();
    });
    QMetaObject::invokeMethod(
        signalTarget ? signalTarget : owner, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
}
void openPlaylist(echo::MainWindow& window, qint64 id)
{
    auto* tree = window.findChild<QTreeWidget*>("playlistTree");
    check(tree, "playlist tree exists");
    QTreeWidgetItem* item = nullptr;
    QTreeWidgetItemIterator iterator(tree);
    while (*iterator) {
        if ((*iterator)->data(0, Qt::UserRole).toLongLong() == id
            && (*iterator)->data(0, Qt::UserRole + 1).toString() == "playlist")
            item = *iterator;
        ++iterator;
    }
    check(item, "playlist navigation entry exists");
    tree->scrollToItem(item);
    click(tree->viewport(), tree->visualItemRect(item).center());
}

}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    const auto runId = QUuid::createUuid().toString(QUuid::Id128);
    app.setOrganizationName(QStringLiteral("EchoFolderTest"));
    app.setApplicationName(runId);
    echo::useDarkTheme();
    QTemporaryDir fixtures;
    const QString a = fixtures.filePath("Alpha"), b = fixtures.filePath("Beta"), empty = fixtures.filePath("Empty");
    wave(a + "/A.wav", 3000);
    wave(a + "/B.wav", 450);
    wave(a + "/Nested/N.wav");
    wave(b + "/C.wav", 450);
    wave(fixtures.filePath("external/X.wav"), 300);
    QDir().mkpath(empty);
    {
        echo::Library library;
        QString error;
        check(library.open(&error), "library opens");
        check(library.roots().isEmpty(), "fresh source list empty");
        check(library.allTracks("album", false).isEmpty(), "fresh song list empty");
        int parsed = 0;
        QObject::connect(&library, &echo::Library::scanStatistics, [&](qint64, int count) { parsed += count; });
        const auto rootA = library.addRoot(a), rootB = library.addRoot(b), emptyId = library.addRoot(empty);
        settled(library);
        check(library.roots().size() == 3, "empty source retained");
        check(library.folderTracks(rootA).size() == 2, "single-level scan");
        check(library.allTracks("album", false).size() == 3, "aggregate direct sources only");
        check(library.addRoot(a + "/.") == rootA, "canonical duplicate source reuses id");
        check(library.addRoot(empty + "/") == emptyId && library.root(emptyId).name == "Empty",
            "trailing separator preserves folder basename");
        parsed = 0;
        library.rescanAll();
        settled(library);
        check(parsed == 0, "unchanged scan does not parse tags");
        const auto X = library.importFiles({ fixtures.filePath("external/X.wav") }).first();
        check(library.allTracks("album", false).size() == 3, "standalone file excluded from global songs");
        check(library.searchTracks("X").isEmpty(), "standalone file excluded from global search");
        QFile m3u(fixtures.filePath("external.m3u"));
        check(m3u.open(QIODevice::WriteOnly), "m3u fixture");
        m3u.write("external/X.wav\nexternal/missing.wav\n");
        m3u.close();
        const auto imported = library.createPlaylist("Imported");
        check(library.importM3u(imported, m3u.fileName(), &error), "m3u imports");
        check(library.playlistTrackIds(imported).size() == 2, "missing m3u reference retained");
        check(library.playlistTracks(imported).last().missing, "missing m3u reference unavailable");
        check(library.allTracks("album", false).size() == 3, "m3u excluded from aggregates");
        check(QFile::rename(fixtures.filePath("external/X.wav"), fixtures.filePath("external/X.saved")),
            "remove standalone file temporarily");
        library.rescanAll();
        settled(library);
        check(library.track(X).missing, "loose-file refresh marks missing reference");
        check(QFile::rename(fixtures.filePath("external/X.saved"), fixtures.filePath("external/X.wav")),
            "restore standalone file");
        library.rescanAll();
        settled(library);
        check(!library.track(X).missing, "loose-file refresh recovers reference");
        const auto tracks = library.folderTracks(rootA);
        const auto A = tracks[0].id, B = tracks[1].id, C = library.folderTracks(rootB).first().id;
        library.setFavorites({ A }, true);
        const auto snapshot = library.createPlaylistFromFolder(rootA, "Snapshot", { A, B });
        check(library.playlistTrackIds(snapshot) == QVector<qint64>({ A, B }), "folder snapshot order");
        library.undo();
        check(library.playlistTrackIds(snapshot).isEmpty(), "snapshot undone in one step");
        library.redo();
        qint64 restoredSnapshot = 0;
        for (const auto& item : library.playlists())
            if (item.name == "Snapshot")
                restoredSnapshot = item.id;
        check(
            library.playlistTrackIds(restoredSnapshot) == QVector<qint64>({ A, B }), "snapshot redo restores members");
        wave(a + "/Later.wav");
        library.refreshRoot(rootA);
        settled(library);
        check(library.folderTracks(rootA).size() == 3 && library.playlistTrackIds(restoredSnapshot).size() == 2,
            "snapshot independent of source additions");
        library.removeRoot(rootA);
        check(library.isFavorite(A), "source removal keeps favorite");
        check(library.playlistTrackIds(restoredSnapshot).size() == 2 && library.track(A).id == A,
            "source removal retains references");
        check(library.allTracks("album", false).size() == 1, "source removal leaves other source visible");
        check(library.addRoot(a) == rootA, "readd source reuses source identity");
        settled(library);
        check(library.folderTracks(rootA).first().id == A, "readd source reuses song identity");
        check(QFile::rename(a + "/B.wav", a + "/B.saved"), "simulate missing file");
        library.refreshRoot(rootA);
        settled(library);
        check(library.track(B).missing && library.allTracks("album", false).size() == 4,
            "missing song stays in aggregates");
        check(QFile::rename(a + "/B.saved", a + "/B.wav"), "restore missing file");
        library.refreshRoot(rootA);
        settled(library);
        check(!library.track(B).missing, "restored file recovers original identity");
        const auto relocated = fixtures.filePath("MovedAlpha");
        check(QDir().rename(a, relocated), "move physical source");
        library.refreshRoot(rootA);
        settled(library);
        check(library.root(rootA).status == "missing" && library.track(A).missing,
            "missing source inherits song availability");
        check(library.relocateRoot(rootA, relocated, &error), "relocate source");
        settled(library);
        check(library.track(A).path == QFileInfo(relocated + "/A.wav").canonicalFilePath() && !library.track(A).missing
                && library.isFavorite(A),
            "source relocation keeps favorite and identity");
        const auto nested = library.addRoot(relocated + "/Nested");
        settled(library);
        check(library.folderTracks(nested).size() == 1, "nested directory requires explicit registration");
        library.removeRoot(nested);
        auto sink = std::make_unique<SilentOutput>();
        auto* capture = sink.get();
        echo::AudioEngine engine(std::move(sink));
        echo::Player player(&library, &engine);
        player.setReplaceContextQueue(false);
        check(!player.replaceContextQueue(), "queue preference stored");
        player.playTracks({ A, B, C }, 0, false);
        check(waitFor([&] { return capture->nonzero > 100 && player.currentTrack().id == A; }),
            "actual PCM plays initial song");
        const auto original = player.queue();
        player.playSinglePreservingQueue(X);
        const auto queue = player.queue();
        check(queue.size() == 4 && queue[0].trackId == A && queue[1].trackId == X && queue[2].trackId == B
                && queue[3].trackId == C,
            "insert and play keeps original order");
        check(queue[0].token == original[0].token && queue[2].token == original[1].token
                && queue[3].token == original[2].token,
            "insertion preserves queue tokens");
        check(waitFor(
                  [&] { return player.currentTrack().id == B && player.state() == echo::AudioEngine::State::Playing; }),
            "after X real playback resumes B");
        check(waitFor(
                  [&] { return player.currentTrack().id == C && player.state() == echo::AudioEngine::State::Playing; }),
            "after B real playback resumes C");
        player.setRepeat(echo::Player::Repeat::One);
        player.playSinglePreservingQueue(A);
        const auto count = player.queue().size();
        player.playSinglePreservingQueue(A);
        check(player.queue().size() == count && player.repeat() == echo::Player::Repeat::One,
            "current song restart does not duplicate and preserves repeat");
        const auto before = player.queue();
        const auto missing = library.playlistTracks(imported).last().id;
        player.playTracks({ missing }, 0, false);
        check(player.queue().size() == before.size() && player.queue().first().token == before.first().token,
            "all unavailable replacement leaves queue intact");
        player.setRepeat(echo::Player::Repeat::Off);
        const auto duplicates = library.createPlaylist("Duplicates");
        library.addToPlaylist(duplicates, { A, B, A });
        auto entries = library.playlistTracks(duplicates);
        check(entries[0].listEntryId != entries[2].listEntryId, "duplicate songs have distinct entry identities");
        library.removePlaylistPositions(duplicates, { 2 });
        const auto remaining = library.playlistTracks(duplicates);
        check(remaining.size() == 2 && remaining[0].listEntryId == entries[0].listEntryId
                && remaining[1].listEntryId == entries[1].listEntryId,
            "removal preserves other entry identities");
        library.undo();
        check(library.playlistTrackIds(duplicates) == QVector<qint64>({ A, B, A }), "duplicate removal undo");
        // Actual widgets, input dispatch, settings, navigation and stable source title.
        echo::MainWindow window(&library, &player, &engine);
        window.resize(1180, 780);
        window.show();
        app.processEvents();
        auto* folders = window.findChild<echo::FoldersPage*>();
        auto* songs = window.findChild<echo::SongsPage*>();
        auto* playlist = window.findChild<echo::PlaylistPage*>();
        check(folders && songs && playlist, "shared track-list pages installed");
        {
            auto* mini = window.findChild<echo::MiniPlayer*>();
            auto* miniSeek = mini->findChild<QSlider*>("miniPlaybackSeek");
            check(miniSeek, "mini player has accessible progress slider");
            player.playTracks({ A }, 0, false, false);
            check(waitFor([&] { return player.currentTrack().id == A && engine.state() == echo::AudioEngine::State::Playing; }),
                "prepare real PCM for mini seek");
            engine.pause();
            check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Paused; }), "pause before mini seek");
            mini->show();
            mini->setPosition(player.positionMs(), library.track(A).durationMs);
            app.processEvents();
            const auto tokens = player.queue();
            const auto position = mini->pos();
            int seeks = 0;
            qint64 requested = -1;
            const auto connection = QObject::connect(mini, &echo::MiniPlayer::seekRequested, [&](qint64 value) {
                ++seeks;
                requested = value;
            });
            const QPoint start(miniSeek->width() / 4, miniSeek->height() / 2);
            const QPoint end(3 * miniSeek->width() / 4, miniSeek->height() / 2);
            QMouseEvent press(QEvent::MouseButtonPress, start, miniSeek->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(miniSeek, &press);
            QMouseEvent move(QEvent::MouseMove, end, miniSeek->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(miniSeek, &move);
            const int dragged = miniSeek->value();
            mini->setPosition(0, library.track(A).durationMs);
            check(miniSeek->value() == dragged && seeks == 0, "mini drag resists playback refresh and commits only on release");
            QMouseEvent release(QEvent::MouseButtonRelease, end, miniSeek->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(miniSeek, &release);
            check(seeks == 1 && requested > 2000 && requested < 2500, "mini drag seeks to pointer position exactly once");
            check(waitFor([&] { return qAbs(player.positionMs() - requested) < 100 && engine.state() == echo::AudioEngine::State::Paused; }),
                "mini seek changes actual paused PCM position without resuming");
            auto* mainSeek = window.findChild<echo::PlayerBar*>()->findChild<QSlider*>("playbackSeek");
            check(waitFor([&] { return qAbs(mainSeek->value() - requested) < 100; }), "main player progress follows mini seek");
            check(mini->pos() == position && player.queue().first().token == tokens.first().token,
                "mini seek preserves window position and queue identity");
            mini->grab().save(QStringLiteral("/tmp/echo-mini-progress.png"));
            QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
            QApplication::sendEvent(miniSeek, &left);
            check(seeks == 2 && requested == 0, "mini slider supports keyboard seeking");
            check(waitFor([&] { return player.positionMs() < 100; }), "keyboard mini seek reaches audio engine");
            QObject::disconnect(connection);
            mini->hide();
        }
        {
            echo::MiniPlayer preview;
            auto current = library.track(A);
            preview.setTrack(current, {}, false);
            preview.setPosition(1000, current.durationMs);
            auto* seek = preview.findChild<QSlider*>("miniPlaybackSeek");
            preview.setPosition(1000, current.durationMs, false);
            check(!seek->isEnabled(), "mini progress disables seeking before audio is loaded");
            preview.setPosition(1000, current.durationMs, true);
            int seeks = 0;
            QObject::connect(&preview, &echo::MiniPlayer::seekRequested, [&](qint64) { ++seeks; });
            const QPoint point(seek->width() / 2, seek->height() / 2);
            QMouseEvent press(QEvent::MouseButtonPress, point, seek->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(seek, &press);
            preview.setTrack(library.track(B), {}, false);
            QMouseEvent release(QEvent::MouseButtonRelease, point, seek->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(seek, &release);
            check(seeks == 0 && !seek->isSliderDown(), "changing track cancels pending mini drag without seeking new song");
            preview.setTrack({}, {}, false);
            preview.setPosition(0, 0);
            check(!seek->isEnabled() && seek->value() == 0, "empty mini player disables seeking");
        }

        auto* navigation = window.findChild<QListWidget*>("libraryNav");
        if (!navigation) {
            for (auto* list : window.findChildren<QListWidget*>())
                if (list->count() == 4 && list->item(3)->text() == QStringLiteral("文件夹"))
                    navigation = list;
        }
        check(navigation, "library navigation found");
        check(navigation->item(2)->text() == QStringLiteral("艺术家"), "artist navigation uses consistent Chinese terminology");
        navigation->setCurrentRow(2);
        app.processEvents();
        auto* artistPage = window.findChild<echo::ArtistsPage*>();
        auto* artistList = artistPage->findChild<QListWidget*>("artistList");
        check(artistList->currentItem()->text() == QStringLiteral("未知艺术家"), "untagged tracks use unknown artist fallback");
        window.resize(1040, 640);
        app.processEvents();
        auto* artistAlbums = artistPage->findChild<QListWidget*>("artistAlbums");
        check(artistAlbums->visualItemRect(artistAlbums->item(0)).bottom() < artistAlbums->viewport()->height(),
            "artist album card and both text lines fit at minimum window size");
        window.grab().save(QStringLiteral("/tmp/echo-artists-unknown.png"));
        navigation->setCurrentRow(1);
        auto* albumsPage = window.findChild<echo::AlbumsPage*>();
        albumsPage->openAlbum(library.albumTracks(library.track(A).albumKey).first().albumKey);
        app.processEvents();
        auto* albumPlay = albumsPage->findChild<QPushButton*>("primaryButton");
        auto* albumShuffle = albumsPage->findChild<QPushButton*>("secondaryButton");
        check(albumPlay->height() == 32 && albumPlay->height() == albumShuffle->height(), "album actions have matching dimensions");
        window.grab().save(QStringLiteral("/tmp/echo-album-unknown.png"));
        auto* artistLink = albumsPage->findChild<QLabel*>("albumArtist");
        click(artistLink, QPoint(10, artistLink->height() / 2));
        check(navigation->currentRow() == 2 && artistList->currentItem()->data(Qt::UserRole).toString() == library.track(A).artistKey,
            "actual unknown artist link preserves special internal key");
        window.resize(1180, 780);

        navigation->setCurrentRow(3);
        app.processEvents();
        auto* tree = folders->findChild<QTreeWidget*>("folderTree");
        check(tree->topLevelItemCount() == 3, "folder list includes empty registered source");
        QTreeWidgetItem* selected = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == rootA)
                selected = tree->topLevelItem(i);
        check(selected, "registered source visible");
        tree->setCurrentItem(selected);
        const auto pos = tree->visualItemRect(selected).center();
        click(tree->viewport(), pos);
        QMouseEvent dbl(QEvent::MouseButtonDblClick, pos, tree->viewport()->mapToGlobal(pos), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &dbl);
        app.processEvents();
        check(folders->detailPage()->sourceId() == rootA && folders->title() == "MovedAlpha",
            "double click opens folder-specific title");
        bool visibleTitle = false;
        for (auto* label : window.findChildren<QLabel*>())
            if (label->isVisible() && label->text() == "MovedAlpha")
                visibleTitle = true;
        check(visibleTitle, "window heading uses folder name");
        settled(library);
        auto* backButton = folders->findChild<QPushButton*>("backToFolders");
        auto* pathLabel = folders->detailPage()->findChild<QLabel*>("trackListPath");
        check(pathLabel && pathLabel->height() == 20 && !pathLabel->wordWrap(), "folder path remains a single line");
        check(backButton->mapTo(&window, QPoint()).x() == pathLabel->mapTo(&window, QPoint()).x(),
            "folder back navigation aligns with detail content");
        window.grab().save(fixtures.filePath("folder-detail.png"));
        window.grab().save(QStringLiteral("/tmp/echo-folder-detail.png"));
        click(folders->findChild<QPushButton*>("backToFolders"), QPoint(20, 12));
        check(tree->isVisible() && tree->currentItem() == selected, "back returns to source selection");
        window.grab().save(QStringLiteral("/tmp/echo-folder-list.png"));
        const auto hoverPoint = tree->visualItemRect(tree->topLevelItem(1)).center();
        QMouseEvent hover(QEvent::MouseMove, hoverPoint, tree->viewport()->mapToGlobal(hoverPoint), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &hover);
        app.processEvents();
        check(tree->property("hoverRow").toInt() == 1, "folder hover tracks the whole row");
        window.grab().save(QStringLiteral("/tmp/echo-folder-hover.png"));
        const QEvent::Type leaveType = QEvent::Leave;
        QEvent leave(leaveType);
        QApplication::sendEvent(tree->viewport(), &leave);
        check(tree->property("hoverRow").toInt() == -1, "folder hover clears on viewport exit");
        window.showSettings();
        app.processEvents();
        auto* toggle = window.findChild<QCheckBox*>("replaceContextQueueToggle");
        check(toggle && !toggle->isChecked(), "settings reflects persisted queue preference");
        click(toggle, QPoint(8, toggle->height() / 2));
        check(player.replaceContextQueue(), "settings checkbox updates policy");
        auto* settings = window.findChild<echo::SettingsDialog*>();
        settings->grab().save(QStringLiteral("/tmp/echo-folder-settings.png"));
        settings->hide();
        // Exercise the shared dialogs with keyboard cancellation and long paths; never remove real sources here.
        {
            echo::MessageDialog remove(QMessageBox::Question, QStringLiteral("移除文件夹"),
                QStringLiteral("歌曲将退出全局资料库。已有歌单、收藏和播放队列会保留引用，原位置的音乐文件也会保留。"),
                QMessageBox::Yes | QMessageBox::Cancel, &window);
            remove.setConfirmText(QStringLiteral("移除文件夹"), true);
            remove.show();
            app.processEvents();
            const auto* description = remove.findChild<QLabel*>("dialogDescription");
            check(description->height() <= description->heightForWidth(description->width()) + 2,
                "short confirmation has no inherited blank text rows");
            check(remove.height() <= 220, "short confirmation remains compact without native title bar");
            check(remove.defaultButton() == remove.button(QMessageBox::Cancel), "destructive dialog defaults to cancel");
            check(remove.windowFlags().testFlag(Qt::FramelessWindowHint) && remove.button(QMessageBox::Yes)->text() == QStringLiteral("移除文件夹"),
                "confirmation uses explicit Chinese action and no oversized stock icon");
            remove.grab().save(QStringLiteral("/tmp/echo-dialog-remove.png"));
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&remove, &escape);
            app.processEvents();
            check(remove.result() == QMessageBox::Cancel && !remove.isVisible(), "Escape cancels source removal");
            remove.show();
            app.processEvents();
            QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(&remove, &returnKey);
            check(remove.result() == QMessageBox::Cancel && !remove.isVisible(), "Return defaults to cancellation on destructive confirmation");
            echo::MessageDialog trash(QMessageBox::Warning, QStringLiteral("移到废纸篓"),
                QStringLiteral("将 2 个音乐文件移到废纸篓？\n会影响 3 首曲目。歌曲引用会保留，封面、歌词和 CUE 文件也会保留。"),
                QMessageBox::Yes | QMessageBox::Cancel, &window);
            trash.setConfirmText(QStringLiteral("移到废纸篓"), true);
            trash.setDetailedText(QStringLiteral("/Users/test/Music/") + QString(240, u'x') + QStringLiteral(".flac\n曲目 A\n曲目 B"));
            trash.show();
            app.processEvents();
            QAbstractButton* details = nullptr;
            for (auto* action : trash.buttons())
                if (action->text() == QStringLiteral("查看详情"))
                    details = action;
            check(details, "trash details button is localized");
            const QPoint collapsedPosition = trash.pos();
            details->click();
            app.processEvents();
            check(trash.pos() == collapsedPosition, "expanding details preserves dialog top position");
            check(trash.findChild<QTextEdit*>()->isVisible(), "long file paths expand in a bounded details view");
            check(trash.width() < 650 && trash.height() < 650, "long paths do not force oversized dialog");
            trash.grab().save(QStringLiteral("/tmp/echo-dialog-trash.png"));
            trash.close();
            echo::MessageDialog failure(QMessageBox::Warning, QStringLiteral("定位失败"),
                QStringLiteral("无法访问所选文件夹，请检查磁盘连接和访问权限。"), QMessageBox::Ok, &window);
            failure.show();
            app.processEvents();
            failure.grab().save(QStringLiteral("/tmp/echo-dialog-error.png"));
            check(failure.button(QMessageBox::Ok)->text() == QStringLiteral("好"), "error acknowledgement is localized");
            failure.close();
            const QString longMessage = QStringLiteral("无法访问以下文件：\n") + QString(1200, u'x');
            echo::MessageDialog longError(QMessageBox::Warning, QStringLiteral("操作失败"), longMessage, QMessageBox::Ok, &window);
            longError.show();
            app.processEvents();
            auto* scrollableMessage = longError.findChild<QTextEdit*>("dialogLongDescription");
            check(scrollableMessage && scrollableMessage->toPlainText() == longMessage && longError.height() < 400,
                "long error messages remain fully readable in bounded dialog");
            longError.grab().save(QStringLiteral("/tmp/echo-dialog-long-error.png"));
            longError.close();
            echo::TrackDialog info(library.track(X), {}, &window);
            info.show();
            app.processEvents();
            info.grab().save(QStringLiteral("/tmp/echo-dialog-track-info.png"));
            info.reject();
            QTimer::singleShot(0, [] {
                auto* input = qobject_cast<echo::NameDialog*>(QApplication::activeModalWidget());
                check(input, "shared naming dialog opens");
                input->setTextValue(QStringLiteral("   "));
                auto* accept = input->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
                check(!accept->isEnabled(), "blank names cannot be submitted");
                input->setTextValue(QStringLiteral("新播放列表"));
                check(accept->isEnabled(), "valid name enables create action");
                input->grab().save(QStringLiteral("/tmp/echo-dialog-name.png"));
                input->reject();
            });
            check(echo::askName(&window, QStringLiteral("创建播放列表"), QStringLiteral("Music"), QStringLiteral("创建")).isEmpty(),
                "cancel naming creates no playlist");
        }
        playlist->setPlaylist(duplicates);
        const auto originalOrder = library.playlistTrackIds(duplicates);
        QMetaObject::invokeMethod(playlist->table()->horizontalHeader(), "sectionClicked", Q_ARG(int, 1));
        check(library.playlistTrackIds(duplicates) == originalOrder, "view sorting does not rewrite playlist order");
        // Create an independent playlist through the actual folder context menu and naming dialog.
        navigation->setCurrentRow(3);
        app.processEvents();
        selected = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == rootA)
                selected = tree->topLevelItem(i);
        check(selected, "folder remains listed");
        menuAction(folders, tree->visualItemRect(selected).center(), QStringLiteral("移除文件夹…"), {}, tree);
        check(library.roots().size() == 3, "cancelling actual removal leaves registered source intact");
        menuAction(folders, tree->visualItemRect(selected).center(), QStringLiteral("创建播放列表…"),
            QStringLiteral("UI Snapshot"), tree);
        qint64 uiSnapshot = 0;
        for (const auto& item : library.playlists())
            if (item.name == "UI Snapshot")
                uiSnapshot = item.id;
        check(uiSnapshot && library.playlistTrackIds(uiSnapshot).size() == 3, "folder menu creates named snapshot");
        // Sorting a duplicate-filled playlist must remove the selected occurrence, not a source row with the same
        // visible index.
        openPlaylist(window, duplicates);
        const auto beforeEntries = library.playlistTracks(duplicates);
        int duplicateRow = -1;
        for (int row = 0; row < playlist->table()->trackModel()->rowCount(); ++row)
            if (playlist->table()->trackModel()->trackAt(row).playlistPosition == 2)
                duplicateRow = row;
        check(duplicateRow >= 0, "sorted duplicate occurrence found");
        const auto modelIndex = playlist->table()->model()->index(duplicateRow, 1);
        playlist->table()->selectionModel()->setCurrentIndex(
            modelIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        menuAction(
            playlist->table(), playlist->table()->visualRect(modelIndex).center(), QStringLiteral("从播放列表移除"));
        check(library.playlistTrackIds(duplicates) == QVector<qint64>({ A, B })
                && library.playlistTracks(duplicates).first().listEntryId == beforeEntries.first().listEntryId,
            "sorted menu removes exact occurrence");
        library.undo();
        // Verify the setting through the visible playlist's Enter activation route.
        openPlaylist(window, imported);
        player.setReplaceContextQueue(false);
        player.playTracks({ A, B, C }, 0, false);
        check(waitFor([&] { return player.currentTrack().id == A && player.queue().size() == 3; }),
            "prepare visible queue-policy test");
        const auto originalToken = player.queue()[0].token;
        int xRow = -1, missingRow = -1;
        for (int row = 0; row < playlist->table()->trackModel()->rowCount(); ++row) {
            const auto item = playlist->table()->trackModel()->trackAt(row);
            if (item.id == X)
                xRow = row;
            if (item.missing)
                missingRow = row;
        }
        check(xRow >= 0 && missingRow >= 0, "standalone and missing playlist rows visible");
        playlist->table()->setCurrentIndex(playlist->table()->model()->index(xRow, 1));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(playlist->table(), &enter);
        check(player.queue().size() == 4 && player.queue()[1].trackId == X && player.queue()[0].token == originalToken,
            "visible Enter honors preserve-queue setting");
        const auto beforeUnavailable = player.queue();
        playlist->table()->setCurrentIndex(playlist->table()->model()->index(missingRow, 1));
        QApplication::sendEvent(playlist->table(), &enter);
        app.processEvents();
        auto* unavailable = window.findChild<echo::MessageDialog*>("unavailableTrackDialog");
        check(unavailable && unavailable->isVisible() && player.queue().size() == beforeUnavailable.size(),
            "missing-song interaction preserves queue");
        unavailable->close();
        player.setReplaceContextQueue(true);
        playlist->table()->setCurrentIndex(playlist->table()->model()->index(xRow, 1));
        QApplication::sendEvent(playlist->table(), &enter);
        check(waitFor([&] { return player.queue().size() == 1 && player.queue().first().trackId == X; }),
            "visible Enter honors replacement setting and skips missing entries");
        player.setReplaceContextQueue(false);
        navigation->setCurrentRow(3);
        folders->openFolder(rootA);
        app.processEvents();
        auto* playAll = folders->detailPage()->findChild<QPushButton*>("playAll");
        click(playAll, playAll->rect().center());
        check(waitFor([&] { return player.queue().size() == 3 && player.queue().first().trackId == A; }),
            "explicit play-all replaces despite disabled implicit replacement");
        // Events from multiple sources and events arriving during a scan must be coalesced without starvation.
        int completed = 0;
        QObject::connect(&library, &echo::Library::scanStatistics, [&](qint64 id, int) {
            if (id == rootA)
                ++completed;
        });
        for (int i = 0; i < 100; ++i)
            library.refreshRoot(rootA);
        library.refreshRoot(rootB);
        settled(library);
        check(completed <= 2, "scan event storm stays bounded");
        // Inject a write failure in one source; the serial scheduler must continue to the next source.
        echo::Database failing;
        check(failing.open(library.databasePath(), "failure-injection", &error), "failure injection connection");
        QSqlQuery sql(QSqlDatabase::database("failure-injection"));
        check(sql.exec("CREATE TRIGGER reject_scan BEFORE INSERT ON tracks WHEN new.title='Blocked' BEGIN SELECT "
                       "RAISE(ABORT,'injected'); END"),
            "scan failure trigger");
        wave(relocated + "/Blocked.wav");
        bool failedScan = false;
        QObject::connect(&library, &echo::Library::scanFailed, [&](const QString&) { failedScan = true; });
        library.refreshRoot(rootA);
        library.refreshRoot(rootB);
        settled(library);
        check(failedScan, "scan failure surfaced without wedging scheduler");
        check(sql.exec("DROP TRIGGER reject_scan"), "remove failure injection");
        QFile::remove(relocated + "/Blocked.wav");
        library.refreshRoot(rootA);
        settled(library);
        // Record cold/warm scan latency, tag parsing and UI responsiveness on a real 1000-file directory.
        const auto perf = fixtures.filePath("Performance");
        wave(perf + "/0000.wav", 40);
        for (int i = 1; i < 1000; ++i)
            check(QFile::copy(perf + "/0000.wav", perf + QStringLiteral("/%1.wav").arg(i, 4, 10, QChar('0'))),
                "performance fixture copy");
        QElapsedTimer scanTime, heartbeatTime;
        qint64 maximumGap = 0;
        heartbeatTime.start();
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QObject::connect(
            &heartbeat, &QTimer::timeout, [&] { maximumGap = std::max(maximumGap, heartbeatTime.restart()); });
        heartbeat.start();
        scanTime.start();
        const auto perfId = library.addRoot(perf);
        settled(library);
        const auto cold = scanTime.elapsed();
        parsed = 0;
        scanTime.restart();
        library.refreshRoot(perfId);
        settled(library);
        const auto warm = scanTime.elapsed();
        heartbeat.stop();
        check(library.root(perfId).tracks == 1000 && parsed == 0, "1000-file warm scan parses zero tags");
        library.removeRoot(perfId);
        qInfo() << "Performance: 1000 files, cold" << cold << "ms, warm" << warm << "ms, maximum UI event gap"
                << maximumGap << "ms";
        // A late result must not resurrect an unregistered source.
        echo::Database db;
        check(db.open(library.databasePath(), "late-result", &error), "late-result connection");
        const auto oldRoot = db.root(emptyId);
        library.removeRoot(emptyId);
        echo::Track phantom;
        phantom.rootId = emptyId;
        phantom.path = empty + "/phantom.wav";
        phantom.folder = empty;
        check(db.commitScan(emptyId, empty, oldRoot.revision, { phantom }, "{}", &error),
            "discarded late scan completes harmlessly");
        check(db.queryTracks("WHERE root_id=?", { emptyId }, { }).isEmpty(), "late result cannot resurrect tracks");
        // Shared CUE audio stays one physical file; cross-directory references cannot import music.
        const auto cueRoot = fixtures.filePath("Cue");
        const auto audio = cueRoot + "/echo-folder-test-" + runId + ".wav";
        wave(audio, 1500);
        QFile cue(cueRoot + "/disc.cue");
        check(cue.open(QIODevice::WriteOnly), "cue fixture");
        cue.write((QStringLiteral("FILE \"") + QFileInfo(audio).fileName()
            + QStringLiteral("\" WAVE\nTRACK 01 AUDIO\nTITLE \"Part One\"\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nTITLE "
                             "\"Part Two\"\nINDEX 01 00:00:40\n"))
                .toUtf8());
        cue.close();
        const auto outsideCue = fixtures.filePath("OutsideCue");
        QDir().mkpath(outsideCue);
        QFile externalCue(outsideCue + "/external.cue");
        check(externalCue.open(QIODevice::WriteOnly), "external cue fixture");
        externalCue.write("FILE \"../MovedAlpha/A.wav\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n");
        externalCue.close();
        const auto outsideId = library.addRoot(outsideCue);
        settled(library);
        check(library.folderTracks(outsideId).isEmpty() && library.track(A).rootId == rootA,
            "cross-directory cue cannot claim another source file");
        const auto cueId = library.addRoot(cueRoot);
        settled(library);
        const auto cueTracks = library.folderTracks(cueId);
        check(cueTracks.size() == 2, "shared CUE audio produces two tracks");
        parsed = 0;
        library.refreshRoot(cueId);
        settled(library);
        check(parsed == 0 && library.folderTracks(cueId).size() == 2,
            "unchanged CUE scan reuses metadata without duplicate whole-file track");
        library.setFavorites({ cueTracks.first().id }, true);
        player.playTracks({ cueTracks.first().id, B }, 0, false);
        check(waitFor([&] {
            return player.currentTrack().id == cueTracks.first().id
                && player.state() == echo::AudioEngine::State::Playing;
        }),
            "play before real temporary trash operation");
        bool trashed = false;
        QObject::connect(&player, &echo::Player::trashFinished, [&](const QString&) { trashed = true; });
        player.trashFiles({ audio });
        check(waitFor([&] { return trashed; }), "asynchronous temporary file trash finishes");
        settled(library);
        check(!QFileInfo::exists(audio) && library.track(cueTracks.first().id).missing
                && library.track(cueTracks.last().id).missing,
            "trash marks all shared-file references unavailable");
        check(QFileInfo::exists(cue.fileName()) && library.isFavorite(cueTracks.first().id),
            "trash retains CUE sidecar and favorite");
        check(waitFor([&] { return player.currentTrack().id == B; }),
            "deleting current file resumes original next playable song");
        for (auto* box : window.findChildren<echo::MessageDialog*>())
            box->close();
#ifdef Q_OS_MACOS
        // Clean only this uniquely named fixture from the system trash if the OS exposes it here.
        const auto trashPath = QDir::homePath() + "/.Trash/" + QFileInfo(audio).fileName();
        if (QFileInfo::exists(trashPath))
            check(QFile::remove(trashPath), "clean unique temporary trash fixture");
#endif
        wave(audio, 1500);
        library.refreshRoot(cueId);
        settled(library);
        check(!library.track(cueTracks.first().id).missing, "restoring audio recovers shared CUE identities");
        const auto relinkedFile = fixtures.filePath("external/Relinked.wav");
        check(QFile::copy(fixtures.filePath("external/X.wav"), relinkedFile), "single-file relocation fixture");
        library.setFavorites({ X }, true);
        check(library.relocateTrack(X, relinkedFile, &error), "single-file relocation succeeds");
        check(library.track(X).displayTitle() == "Relinked" && library.track(X).rootId == 0 && library.isFavorite(X),
            "single-file relocation refreshes metadata and preserves favorite identity");
        const auto oldPath = library.track(X).path;
        check(!library.relocateTrack(X, library.track(C).path, &error) && library.track(X).path == oldPath,
            "single-file collision leaves references unchanged");
        // A trash failure must preserve both the physical file and current queue.
        const auto queueBeforeFailure = player.queue();
        trashed = false;
        player.trashFiles({ fixtures.filePath("does-not-exist.wav") });
        check(waitFor([&] { return trashed; }), "trash failure returns");
        check(player.queue().size() == queueBeforeFailure.size(), "trash failure preserves queue");
        for (auto* box : window.findChildren<echo::MessageDialog*>())
            box->close();
        player.playTracks({ missing }, 0, false);
        engine.releaseFiles();
        check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Stopped; }), "test playback stops");
        settled(library);
        const auto cpuStart = std::clock();
        QEventLoop idle;
        QTimer::singleShot(500, &idle, &QEventLoop::quit);
        idle.exec();
        qInfo() << "Idle process CPU over 500 ms:" << (100.0 * double(std::clock() - cpuStart) / CLOCKS_PER_SEC / .5)
                << "percent";
        window.hide();
    }
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    qInfo() << checks << "folder, queue, availability, trash, navigation and PCM integration checks passed";
}
