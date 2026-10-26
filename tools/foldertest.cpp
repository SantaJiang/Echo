#include <QBuffer>
#include <QSemaphore>
#include <QCryptographicHash>
#include <QThreadPool>
#include <QImage>
#include <QtConcurrent>
#include "library/library.h"
#include "test_support/support.h"
#include "library/scanner.h"
#include "library/database.h"
#include "playback/player.h"
#include "presentation/theme.h"
#include <QGuiApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QTimer>
#include <QEventLoop>
#include <QSqlQuery>
#include <ctime>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <atomic>
#include <cmath>
#include <thread>

namespace {
using echo::test::SilentOutput;
using echo::test::check;
using echo::test::waitFor;
void settled(echo::Library& library)
{
    check(waitFor([&] { return !library.scanBusy(); }), "scan settles");
}
void wave(const QString& path, int milliseconds = 350)
{
    echo::test::writePcm16Wave(path, milliseconds);
}
}
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
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
        // Search workers use a thread-owned, read-only connection and preserve source scoping.
        auto searchFuture = QtConcurrent::run([path = library.databasePath()] {
            return echo::Library::searchDatabase(path, QStringLiteral("X"));
        });
        searchFuture.waitForFinished();
        check(searchFuture.result().tracks.isEmpty(), "background search excludes loose tracks");
        check(!echo::Library::searchDatabase(library.databasePath(), QStringLiteral("A")).tracks.isEmpty(),
            "read-only background search finds library tracks");
        {
            echo::Database searchDb;
            check(searchDb.open(library.databasePath(), "unicode-review"), "open Unicode search fixture connection");
            const auto original = library.track(A);
            auto unicode = original; unicode.title = QString::fromUtf8("Écho 100%_测试");
            check(searchDb.upsertTrack(unicode) == A, "store Unicode search fixture");
            check(searchDb.searchTracksContaining(QString::fromUtf8("é"), 200).size() == 1,
                "fallback search folds non-ASCII case");
            check(searchDb.searchTracksContaining("%_", 200).size() == 1, "fallback search treats SQL wildcards literally");
            const auto unicodeResults = echo::Library::searchDatabase(library.databasePath(), QString::fromUtf8("é")).tracks;
            bool foundUnicode = false;
            for (const auto& result : unicodeResults) foundUnicode |= result.id == A;
            check(foundUnicode, "background search preserves Unicode matching");
            check(searchDb.upsertTrack(original) == A, "restore search fixture title");
        }
        // More than the old 256-entry threshold must not flush still-budgeted thumbnails.
        QString firstCover;
        int completedCovers = 0;
        QObject::connect(library.artwork(), &echo::ArtworkCache::ready, &library,
            [&](const QString&, int size) { if (size == 24) ++completedCovers; });
        for (int i = 0; i < 270; ++i) {
            QImage image(24, 24, QImage::Format_RGB32);
            image.fill(QColor::fromRgb((i * 7919) & 0xffffff));
            QByteArray bytes;
            QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
            const QString hash = library.artwork()->store(bytes);
            if (i == 0) firstCover = hash;
            check(library.artwork()->pixmap(hash, 24).isNull(), "uncached artwork returns without synchronous decode");
            check(waitFor([&] { return completedCovers == i + 1; }), "async thumbnail completes");
            check(!library.artwork()->pixmap(hash, 24).isNull(), "decoded thumbnail becomes available");
        }
        check(!library.artwork()->pixmap(firstCover, 24).isNull(), "thumbnail cache retains entries under memory budget beyond 256 keys");
        {
            // Enqueue more than the old request limit without processing completion events.
            int received = 0;
            bool playerCoverReady = false;
            const auto connection = QObject::connect(library.artwork(), &echo::ArtworkCache::ready, &library,
                [&](const QString& hash, int size) { if (hash == firstCover && size == 160) playerCoverReady = true;
                    if (hash == firstCover && size >= 100 && size < 140) ++received; });
            for (int size = 100; size < 140; ++size) (void)library.artwork()->pixmap(firstCover, size);
            (void)library.artwork()->pixmap(firstCover, 160);
            check(waitFor([&] { return playerCoverReady && received == 40; }), "saturated thumbnail queue preserves every one-shot request");
            QObject::disconnect(connection);
            QImage panorama(2, 8000, QImage::Format_RGB32); panorama.fill(Qt::red);
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); panorama.save(&buffer, "PNG");
            const QString hash = library.artwork()->store(bytes);
            (void)library.artwork()->pixmap(hash, 160);
            check(waitFor([&] { return !library.artwork()->pixmap(hash, 160).isNull(); }), "panoramic cover decodes within thumbnail allocation budget");
        }
        {
            QFile cached(library.artwork()->filePath(firstCover));
            check(cached.open(QIODevice::ReadOnly), "read recoverable cached cover");
            const QByteArray bytes = cached.readAll(); cached.close();
            check(cached.remove(), "remove temporary cached cover");
            bool failureCompleted = false;
            const auto connection = QObject::connect(library.artwork(), &echo::ArtworkCache::ready, &library,
                [&](const QString& hash, int size) { if (hash == firstCover && size == 777) failureCompleted = true; });
            (void)library.artwork()->pixmap(firstCover, 777);
            check(waitFor([&] { return failureCompleted; }), "missing cover decode finishes without blocking");
            check(library.artwork()->pixmap(firstCover, 777).isNull(), "missing thumbnail is negatively cached");
            check(library.artwork()->store(bytes) == firstCover, "restoring cover retains stable hash");
            check(waitFor([&] { return !library.artwork()->pixmap(firstCover, 777).isNull(); }),
                "restored cover retries failed one-shot thumbnail automatically");
            QObject::disconnect(connection);
        }
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
        {
            // Hold the old measurement in the queue so relocation deterministically wins the race.
            player.setNormalizationEnabled(false);
            check(QThreadPool::globalInstance()->waitForDone(3000), "settle prior background work before relocation race");
            app.processEvents();
            const QString oldAudio = fixtures.filePath("analysis-old.wav"), newAudio = fixtures.filePath("analysis-new.wav");
            wave(oldAudio, 600);
            check(QFile::copy(oldAudio, newAudio), "copy relocation analysis fixture");
            const auto id = library.importFiles({oldAudio}).first();
            player.playTracks({id}, 0, false, false);
            check(waitFor([&] { return player.currentTrack().id == id && engine.state() == echo::AudioEngine::State::Playing; }),
                "start relocation analysis fixture");
            const auto keyFor = [&](const echo::Track& track) {
                const QFileInfo file(track.path);
                const QString source = QStringLiteral("r128-v1|%1|%2|%3|%4|%5").arg(track.path).arg(file.size())
                    .arg(file.lastModified().toMSecsSinceEpoch()).arg(track.cueStartMs).arg(track.cueEndMs);
                return QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
            };
            const QString oldKey = keyFor(library.track(id));
            auto* pool = QThreadPool::globalInstance();
            const int oldMaximum = pool->maxThreadCount();
            pool->setMaxThreadCount(1);
            QSemaphore entered, resume;
            auto blocker = QtConcurrent::run([&] { entered.release(); resume.acquire(); });
            check(entered.tryAcquire(1, 3000), "hold global worker before scheduling loudness analysis");
            player.setNormalizationEnabled(true);
            check(library.relocateTrack(id, newAudio, &error), "relocate track while old analysis is queued");
            const QString newKey = keyFor(library.track(id));
            resume.release();
            blocker.waitForFinished();
            pool->setMaxThreadCount(oldMaximum);
            check(waitFor([&] { return !library.setting("loudness/" + newKey).isEmpty(); }),
                "relocated file receives its own loudness analysis");
            check(library.setting("loudness/" + oldKey).isEmpty(), "old analysis cannot populate cache after relocation");
            player.setNormalizationEnabled(false);
        }
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
    }
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    qInfo() << echo::test::passedChecks() << "folder, queue, availability, trash, navigation and PCM integration checks passed";
}
