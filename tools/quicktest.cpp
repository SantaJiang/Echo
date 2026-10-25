#include "library/library.h"
#include "library/artwork.h"
#include "playback/player.h"
#include "quick/client.h"
#include "quick/playbackview.h"
#include "quick/quickview.h"
#include "ui/theme.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEnterEvent>
#include <QFile>
#include <QPainter>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QSignalSpy>
#include <QScreen>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace {
int checks = 0;
QStringList qmlIssues;
void messages(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type == QtWarningMsg && message.contains("qrc:/echo/quick"))
        qmlIssues << message;
    fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
}
void check(bool ok, const char* message)
{
    if (!ok)
        qFatal("FAIL %s", message);
    ++checks;
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 8000)
{
    QElapsedTimer clock;
    clock.start();
    do {
        QGuiApplication::processEvents();
        if (predicate())
            return true;
        QTest::qWait(5);
    } while (clock.elapsed() < timeout);
    return false;
}
void pump(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
class SilentOutput final : public echo::AudioOutput {
public:
    ~SilentOutput() override { close(); }
    bool open(const echo::SampleFormat& source, bool, echo::DeviceFormat* format, QString*) override
    {
        rate = source.sampleRate;
        channels = source.channels;
        *format = { rate, channels, 32, 4, true };
        opened = true;
        return true;
    }
    void close() override
    {
        pause();
        opened = false;
    }
    void setPull(Pull value) override { pull = std::move(value); }
    void start() override
    {
        pause();
        running = true;
        worker = std::thread([this] {
            std::vector<float> samples(rate / 200 * channels);
            while (running) {
                int got = pull(reinterpret_cast<uint8_t*>(samples.data()), rate / 200);
                if (got < rate / 200)
                    ++underruns;
                if (got) {
                    ++blocks;
                    for (int i = 0; i < got * channels; ++i)
                        if (std::abs(samples[i]) > .01)
                            ++nonzero;
                }
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
    bool isOpen() const override { return opened; }
    std::atomic<int> blocks { 0 }, nonzero { 0 }, underruns { 0 };

private:
    Pull pull;
    int rate = 48000, channels = 1;
    bool opened = false;
    std::atomic<bool> running { false };
    std::thread worker;
};
void wave(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "fixture opens");
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    constexpr int frames = 48000 * 20;
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(frames * 2);
    for (int i = 0; i < frames; ++i)
        out << qint16(std::sin(i * 2 * 3.141592653589793 * 440 / 48000) * 5000);
}
}
int main(int argc, char** argv)
{
    QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
    QGuiApplication app(argc, argv);
    echo::useDarkTheme();
    qInstallMessageHandler(messages);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("EchoQuickTest"));
    QTemporaryDir temp;
    // Each run owns its database; it never touches the real Echo library.
    QCoreApplication::setApplicationName("EchoQuickTest-" + QFileInfo(temp.path()).fileName());
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto cleanup = qScopeGuard([dataPath] {
        QDir(dataPath).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    });
    wave(temp.filePath("Night.wav"));
    {
        echo::Library library;
        QString error;
        check(library.open(&error), "library opens");
        const auto ids = library.importFiles({ temp.filePath("Night.wav") });
        check(ids.size() == 1, "fixture imported");
        auto track = library.track(ids[0]);
        echo::TagEdit tags;
        tags.title = QStringLiteral("夜航 · Nightfall");
        tags.artist = "Echo Sessions";
        tags.album = QStringLiteral("留声之间");
        tags.editLyrics = true;
        tags.lyrics = "[00:00.00]Let the music find its way\n[00:01.00]Across the quiet of the night\n[00:02.00]Every "
                      "note becomes a memory\n[00:04.00]Every silence holds a light\n[00:07.00]And we keep listening";
        QImage artwork(800, 800, QImage::Format_RGB32);
        {
            QPainter paint(&artwork);
            QLinearGradient gradient(0, 0, 800, 800);
            gradient.setColorAt(0, QColor("#35305f"));
            gradient.setColorAt(.5, QColor("#b2678c"));
            gradient.setColorAt(1, QColor("#edba91"));
            paint.fillRect(artwork.rect(), gradient);
            paint.setPen(QColor("#ccffffff"));
            paint.setFont(QFont("Helvetica", 48));
            paint.drawText(QRect(60, 60, 680, 250), Qt::AlignLeft, "ECHO\nSESSIONS");
            paint.setFont(QFont("Helvetica", 20));
            paint.drawText(QPoint(60, 730), "N I G H T F A L L");
        }
        QBuffer encoded(&tags.artwork);
        encoded.open(QIODevice::WriteOnly);
        artwork.save(&encoded, "PNG");
        tags.editArtwork = true;
        tags.artworkMime = "image/png";
        check(library.saveTags(ids[0], tags, &error), "lyrics saved");
        check(QFile::copy(temp.filePath("Night.wav"), temp.filePath("Dawn.wav")), "second fixture copied");
        check(QFile::copy(temp.filePath("Night.wav"), temp.filePath("Memory.wav")), "third fixture copied");
        const auto otherIds = library.importFiles({ temp.filePath("Dawn.wav"), temp.filePath("Memory.wav") });
        check(otherIds.size() == 2, "additional fixtures imported");
        tags.title = QStringLiteral("晨光");
        check(library.saveTags(otherIds[0], tags, &error), "second title saved");
        tags.title = QStringLiteral("Memory");
        tags.album = QStringLiteral("来信");
        tags.artwork.clear(); // Exercise the default cover beside real embedded artwork.
        check(library.saveTags(otherIds[1], tags, &error), "third album saved");
        const auto sourceId = library.addRoot(temp.path());
        check(sourceId > 0, "registered music source");
        check(waitFor([&] { return !library.scanBusy(); }), "source scan completes");
        const auto coverHash = library.track(ids[0]).artworkHash;
        const auto coverPath = library.artwork()->filePath(coverHash);
        check(!coverHash.isEmpty() && QFile::remove(coverPath), "simulate cleared cover cache");
        library.refreshRoot(sourceId);
        check(waitFor([&] { return !library.scanBusy(); }), "cache repair scan completes");
        check(QFile::exists(coverPath) && !QImage(coverPath).isNull(),
            "unchanged music restores missing cover from embedded tags");
        auto output = std::make_unique<SilentOutput>();
        auto* probe = output.get();
        echo::AudioEngine engine(std::move(output));
        echo::Player player(&library, &engine);
        player.setSpatialEnabled(false);
        player.setReplayGainEnabled(false);
        echo::PlaybackView playback(&library, &player, &engine);
        echo::QuickView window(&playback);
        check(window.status() == QQuickView::Ready, "QML loads");
        if (app.arguments().contains("--qml-smoke")) {
            echo::Client smokeClient(&library, &player, &engine);
            echo::QuickView smokeWindow(&playback, &smokeClient);
            check(smokeWindow.status() == QQuickView::Ready, "headless shell QML loads");
            check(waitFor([&] { return !smokeClient.page().value("busy").toBool(); }), "headless library model loads");
            check(qmlIssues.isEmpty(), "headless QML has no warnings");
            QTimer::singleShot(0, &smokeClient, [&smokeClient] { smokeClient.action("quit"); });
            check(app.exec() == 0, "explicit quit exits the application loop");
            qInfo() << "PASS" << checks << "headless QML startup checks; GPU interaction suite runs separately";
            return 0;
        }
        if (app.arguments().contains("--artists-test")) {
            tags.artist = QStringLiteral("曾沛慈");
            check(library.saveTags(ids[0], tags, &error), "named artist fixture saved");
            tags.artist.clear();
            check(library.saveTags(otherIds[1], tags, &error), "unknown artist fixture saved");
            const auto artists = library.artists();
            check(artists.size() == 3 && artists.last().key == echo::makeArtistKey({}),
                "unknown artist sorts after named artists");
            echo::Client client(&library, &player, &engine);
            echo::QuickView shell(&playback, &client);
            client.attach(&shell);
            client.setReduceMotion(true);
            shell.resize(1200, 800);
            shell.show();
            check(QTest::qWaitForWindowExposed(&shell), "artist window exposed");
            client.navigate("artists", {}, QStringLiteral("艺术家"));
            check(waitFor([&] { return !client.page().value("busy").toBool() && client.cards()->count() == 3; }),
                "artist list loads real library groups");
            auto* list = shell.rootObject()->findChild<QQuickItem*>("artistList");
            auto* grid = shell.rootObject()->findChild<QQuickItem*>("cardGrid");
            check(list && list->isVisible() && !grid->isVisible(), "artists use list instead of cover grid");
            std::function<QQuickItem*(QQuickItem*, const QString&)> findItem = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
                if (parent->objectName() == name) return parent;
                for (auto* child : parent->childItems())
                    if (auto* found = findItem(child, name)) return found;
                return nullptr;
            };
            check(waitFor([&] { return findItem(list, "artistRow2") != nullptr; }), "artist rows instantiated");
            auto* first = findItem(list, "artistRow0");
            auto* last = findItem(list, "artistRow2");
            check(first->width() == list->width() && first->height() == 96, "artist rows fill width at compact height");
            check(last->property("item").toMap().value("title").toString() == QStringLiteral("未知艺术家"),
                "unknown artist appears in last row");
            pump(200);
            QDir().mkpath("/tmp/echo-quick-qa");
            check(shell.grabWindow().save("/tmp/echo-quick-qa/artists.png"), "artist list screenshot saved");
            const auto firstKey = client.cards()->get(0).value("key");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, first->mapToScene(QPointF(120, 45)).toPoint());
            check(waitFor([&] { return !client.page().value("busy").toBool() && client.page().value("kind") == "artist"; }),
                "clicking artist row opens detail");
            check(client.tracks()->count() > 0 && client.tracks()->get(0).value("artist").toString() == artists.first().name,
                "artist detail contains selected artist tracks");
            client.back();
            check(waitFor([&] { return !client.page().value("busy").toBool() && list->isVisible(); }), "back returns to artist list");
            check(client.cards()->get(0).value("key") == firstKey, "back preserves artist order");
            shell.resize(980, 680);
            shell.rootObject()->setProperty("queueOpen", true);
            pump(350);
            first = findItem(list, "artistRow0");
            check(first && first->width() == list->width() && list->width() > 200, "artist list adapts to narrow window and queue");
            check(shell.grabWindow().save("/tmp/echo-quick-qa/artists-narrow.png"), "narrow artist list screenshot saved");
            client.search(QStringLiteral("曾沛慈"));
            check(waitFor([&] { return !client.page().value("busy").toBool() && client.page().value("kind") == "search"; }),
                "artist search completes");
            check(!list->isVisible() && grid->isVisible(), "mixed search keeps card layout");
            bool foundArtist = false;
            for (int i = 0; i < client.cards()->count(); ++i) {
                if (client.cards()->get(i).value("kind") != "artist") continue;
                foundArtist = true;
                auto* cover = findItem(grid, QString("cardArtwork%1").arg(i));
                check(cover && !cover->isVisible(), "search artist does not display generic cover");
            }
            check(foundArtist, "search includes matching artist");
            check(qmlIssues.isEmpty(), "artist list and search have no QML warnings");
            qInfo() << "PASS" << checks << "artist layout and navigation checks";
            return 0;
        }
        const auto verifyFading = [&](echo::QuickView& shell, echo::Client& client) {
            const auto object = [&](const char* name) { return shell.rootObject()->findChild<QObject*>(name); };
            auto* fading = qobject_cast<QQuickItem*>(object("fadingSetting"));
            check(fading && fading->property("checked").toBool(), "fading defaults on in settings");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                fading->mapToScene(QPointF(fading->width() / 2, fading->height() / 2)).toPoint());
            check(waitFor([&] { return !player.fadingEnabled() && library.setting("fading") == "0"; }),
                "settings click disables and persists fading");
            {
                echo::AudioEngine restoredEngine(std::make_unique<SilentOutput>());
                echo::Player restoredPlayer(&library, &restoredEngine);
                restoredPlayer.restoreSession();
                check(!restoredPlayer.fadingEnabled(), "fading choice survives player recreation");
            }
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                fading->mapToScene(QPointF(fading->width() / 2, fading->height() / 2)).toPoint());
            check(waitFor([&] { return player.fadingEnabled() && library.setting("fading") == "1"; }),
                "settings click re-enables fading");
        };
        if (app.arguments().contains("--fading-test")) {
            echo::Client client(&library, &player, &engine);
            echo::QuickView shell(&playback, &client);
            client.attach(&shell);
            shell.show();
            check(QTest::qWaitForWindowExposed(&shell), "fading settings window exposed");
            client.showSettings();
            auto* settings = shell.rootObject()->findChild<QObject*>("settingsDialog");
            check(waitFor([&] { return settings->property("opened").toBool(); }), "fading settings opens");
            verifyFading(shell, client);
            QDir().mkpath("/tmp/echo-quick-qa");
            check(shell.grabWindow().save("/tmp/echo-quick-qa/settings-fading.png"), "fading settings screenshot saved");
            check(qmlIssues.isEmpty(), "fading settings QML has no warnings");
            qInfo() << "PASS" << checks << "fading settings and persistence checks";
            return 0;
        }
        if (app.arguments().contains("--fullscreen-close-test")) {
            QTimer::singleShot(0, &app, [&] {
                echo::Client client(&library, &player, &engine);
                echo::QuickView shell(&playback, &client);
                client.attach(&shell);
                client.showMainWindow();
                check(QTest::qWaitForWindowExposed(&shell), "fullscreen close fixture exposed");
                player.setRepeat(echo::Player::Repeat::One);
                player.playTracks(ids, 0, false);
                check(waitFor([&] { return playback.playing(); }), "background audio starts");
                const auto originalSize = shell.size();
                qInfo() << "Ready for fullscreen" << shell.size() << shell.screen()->size();
                if (!app.arguments().contains("--manual-fullscreen")) shell.showFullScreen();
                check(waitFor([&] { return shell.property("echoNativeFullscreen").toBool(); }, 120000),
                    "AppKit confirms native fullscreen entered");
                qInfo() << "Native fullscreen entered" << shell.size() << shell.fullscreen();
                pump(700);
                QSignalSpy hidden(&shell, &echo::QuickView::hiddenToTray);
                shell.closeWindow();
                shell.closeWindow();
                check(waitFor([&] { return !shell.isVisible() && !shell.fullscreen() && !shell.property("echoNativeFullscreen").toBool(); }), "close leaves fullscreen before hiding");
                check(hidden.count() == 1 && playback.playing(), "close preserves background playback and emits once");
                pump(700);
                client.showMainWindow();
                check(waitFor([&] { return shell.isVisible() && !shell.fullscreen(); }), "resident action reopens a normal window");
                check(shell.size() == originalSize && shell.frameMargins().top() == 0, "reopen restores size and window chrome");
                const auto enterFullscreen = [&] {
                    shell.showFullScreen();
                    check(waitFor([&] { return shell.property("echoNativeFullscreen").toBool(); }), "re-enter native fullscreen");
                    pump(150);
                };
                enterFullscreen();
                shell.closeWindow();
                client.showMainWindow();
                pump(1200);
                check(shell.isVisible() && !shell.fullscreen() && !shell.property("echoNativeFullscreen").toBool()
                        && hidden.count() == 1, "reopen during native exit cancels the pending hide");
                enterFullscreen();
                client.showMini();
                check(waitFor([&] { return !shell.isVisible() && !shell.property("echoNativeFullscreen").toBool(); }),
                    "mini-player switch releases fullscreen Space before hiding");
                bool miniVisible = false;
                for (auto* candidate : QGuiApplication::allWindows())
                    miniVisible |= candidate->property("echoMiniPlayer").toBool() && candidate->isVisible();
                check(miniVisible && playback.playing(), "mini-player remains visible with background playback");
                client.showMainWindow();
                check(waitFor([&] { return shell.isVisible() && !shell.fullscreen(); }), "main window restores after fullscreen mini-player switch");
                qInfo() << "PASS" << checks << "native fullscreen close checks";
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(0); });
            });
            return app.exec();
        }
        if (app.arguments().contains("--detail-layout-test") || app.arguments().contains("--detail-resize-test")) {
            echo::Client client(&library, &player, &engine);
            echo::QuickView shell(&playback, &client);
            client.attach(&shell);
            client.setReduceMotion(true);
            player.playTracks(ids, 0, false);
            check(waitFor([&] { return playback.playing(); }), "detail fixture plays");
            player.toggle();
            check(waitFor([&] { return !playback.playing(); }), "detail fixture pauses");
            shell.rootObject()->setProperty("expanded", true);
            shell.show();
            check(QTest::qWaitForWindowExposed(&shell), "detail layout window exposed");
            auto* detail = shell.rootObject()->findChild<QQuickItem*>("nowPlaying");
            auto* content = detail->findChild<QQuickItem*>("detailContent");
            auto* cover = detail->findChild<QQuickItem*>("detailCover");
            auto* lyrics = detail->findChild<QQuickItem*>("lyricsView");
            auto* button = shell.rootObject()->findChild<QQuickItem*>("barToggle");
            auto* songInfo = detail->findChild<QQuickItem*>("detailSongInfo");
            check(content && cover && lyrics && button && songInfo, "detail layout surfaces exist");
            QVariantList lines;
            const QStringList text = {QStringLiteral("数到十 - 曾沛慈"), QStringLiteral("优雅的蝴蝶 自由穿梭在花蕊"),
                QStringLiteral("沉默的落叶 风来了就要道别"), QStringLiteral("不同的世界 没有错与对"),
                QStringLiteral("人有各自的终点"), QStringLiteral("你看不见 心酸那些"),
                QStringLiteral("天亮了又黑 爱走了才又后悔"), QStringLiteral("我们都在学 每天都勇敢一点"),
                QStringLiteral("在这个人间 路过了伤悲"), QStringLiteral("距离幸福就不远"),
                QStringLiteral("抬头让阳光晒干我的泪"), QStringLiteral("爱上一个人 两颗心 三分想念")};
            for (int i = 0; i < text.size(); ++i) lines << QVariantMap{{"text", text[i]}, {"time", i * 1000}};
            lyrics->setProperty("model", lines);
            lyrics->setProperty("activeLyricIndex", 0);
            QDir().mkpath("/tmp/echo-detail-layout");
            for (const QSize size : {QSize(1920, 1050), QSize(1440, 900), QSize(1180, 800), QSize(1040, 680)}) {
                shell.resize(size);
                const bool layoutReady = waitFor([&] {
                    return std::abs(detail->property("layoutWidth").toReal() - shell.width()) < .1
                        && std::abs(detail->property("layoutHeight").toReal() - shell.height()) < .1
                        && std::abs(cover->width() - std::min({520., content->width() * .43, content->height() * .88})) < 1;
                });
                if (!layoutReady) qInfo() << "Unsettled layout" << shell.size()
                    << detail->property("layoutWidth") << detail->property("layoutHeight")
                    << content->size() << cover->size() << client.reduceMotion();
                check(layoutReady, "playback layout reaches the requested size");
                pump(100);
                const auto body = content->mapRectToScene(content->boundingRect());
                const auto art = cover->mapRectToScene(cover->boundingRect());
                const auto lyricArea = lyrics->mapRectToScene(lyrics->boundingRect());
                const auto controls = button->mapRectToScene(button->boundingRect());
                check(std::abs(body.center().x() - shell.width() / 2. - std::min(80., shell.width() * .045)) < 1,
                    "two-column composition has a proportional optical offset to the right");
                const auto originalCoverSize = std::min({520., body.width() * .43, body.height() * .88});
                check(std::abs(art.width() - originalCoverSize) < 1,
                    "optical offset preserves the existing cover size");
                check(art.right() < lyricArea.left() && lyricArea.right() < shell.width(), "cover and lyrics remain separated and inside window");
                check(controls.bottom() < shell.height() && art.top() > 60, "cover and playback controls stay inside window");
                const auto info = songInfo->mapRectToScene(songInfo->boundingRect());
                check(info.bottom() + 12 <= lyricArea.top() && lyricArea.bottom() <= body.bottom(),
                    "song information sits above the lyric viewport with breathing room");
                check(std::abs(controls.center().x() - shell.width() / 2.) < 1 && controls.width() == 56,
                    "shared playback control stays centered in the window");
                if (shell.width() >= 1920 && shell.height() >= 900) {
                    check(art.left() >= 200 && lyricArea.left() >= 900 && lyricArea.width() <= 641,
                        "fullscreen proportions distribute both columns across canvas");
                }
                check(shell.grabWindow().save(QString("/tmp/echo-detail-layout/detail-%1x%2.png").arg(size.width()).arg(size.height())),
                    "detail layout screenshot saved");
                qInfo() << "Detail layout" << shell.size() << "requested" << size << "DPR" << shell.devicePixelRatio() << "cover" << art << "lyrics" << lyricArea << "play" << controls;
            }
            if (app.arguments().contains("--detail-resize-test")) {
                QTimer::singleShot(0, &app, [&] {
                    const auto coverRect = [&] { return cover->mapRectToScene(cover->boundingRect()); };
                    const auto settled = [&] {
                        return !detail->property("layoutResizing").toBool()
                            && std::abs(detail->property("layoutWidth").toReal() - shell.width()) < .1
                            && std::abs(detail->property("layoutHeight").toReal() - shell.height()) < .1;
                    };
                    shell.resize(1180, 800);
                    pump(100);
                    client.setReduceMotion(false);
                    const auto small = coverRect();
                    const auto textureSize = cover->property("renderTextureSize").toSize();
                    QVector<QRectF> resizeFrames;
                    const auto capture = QObject::connect(cover, &QQuickItem::widthChanged, [&] { resizeFrames << coverRect(); });
                    shell.resize(1440, 900);
                    check(waitFor(settled), "enlarging reaches the final canvas dimensions");
                    pump(40);
                    const auto large = coverRect();
                    check(std::count_if(resizeFrames.begin(), resizeFrames.end(), [&](const auto& frame) {
                            return frame.width() > small.width() + .1 && frame.width() < large.width() - .1;
                        }) >= 4, "enlarging the window renders intermediate playback layouts");
                    check(std::any_of(resizeFrames.begin(), resizeFrames.end(), [&](const auto& frame) {
                            return frame.width() > small.width() && frame.width() < large.width()
                                && frame.x() > small.x() && frame.x() < large.x();
                        }),
                        "cover size and horizontal placement interpolate together");
                    resizeFrames.clear();
                    shell.resize(1180, 800);
                    check(waitFor(settled), "shrinking reaches the final canvas dimensions");
                    check(std::count_if(resizeFrames.begin(), resizeFrames.end(), [&](const auto& frame) {
                            return frame.width() > small.width() + .1 && frame.width() < large.width() - .1;
                        }) >= 4,
                        "shrinking the window also renders an intermediate cover size");
                    // Reverse direction before the first animation finishes.
                    shell.resize(1440, 900);
                    pump(50);
                    shell.resize(1180, 800);
                    check(waitFor(settled), "a resize in mid-transition reaches the latest target");
                    QObject::disconnect(capture);
                    check(cover->property("renderTextureSize").toSize() == textureSize,
                        "cover mask texture stays allocated at the same size throughout resizing");
                    client.setReduceMotion(true);
                    shell.resize(1440, 900);
                    pump(40);
                    check(settled() && std::abs(coverRect().width() - large.width()) < 1,
                        "reduced motion applies the final playback geometry immediately");
                    shell.resize(1180, 800);
                    pump(40);
                    client.setReduceMotion(false);
                    lyrics->setProperty("activeLyricIndex", 6);
                    pump(500);
                    const auto verifyNativeTransition = [&](bool entering) {
                        QVector<QRectF> frames;
                        QTimer sampler;
                        sampler.setInterval(16);
                        QObject::connect(&sampler, &QTimer::timeout, [&] { frames << coverRect(); });
                        frames << coverRect();
                        sampler.start();
                        if (entering) shell.showFullScreen(); else shell.showNormal();
                        check(waitFor([&] {
                            return shell.fullscreen() == entering && settled()
                                && shell.property("echoFullscreenTransition").toString().isEmpty()
                                && (QGuiApplication::platformName() != "cocoa"
                                    || shell.property("echoNativeFullscreen").toBool() == entering);
                        }), "native fullscreen transition finishes with the playback canvas settled");
                        pump(100);
                        sampler.stop();
                        frames << coverRect();
                        int changes = 0;
                        double largestStep = 0;
                        for (int i = 1; i < frames.size(); ++i) {
                            const double step = std::abs(frames[i].x() - frames[i - 1].x())
                                + std::abs(frames[i].width() - frames[i - 1].width());
                            if (step > .01) ++changes;
                            largestStep = std::max(largestStep, step);
                        }
                        const double travel = std::abs(frames.last().x() - frames.first().x())
                            + std::abs(frames.last().width() - frames.first().width());
                        qInfo() << "Native playback resize" << entering << "samples" << frames.size()
                                << "changes" << changes << "largest step" << largestStep << "travel" << travel;
                        check(changes >= 4 && largestStep < travel * .65,
                            "native fullscreen renders multiple layout steps without a single large jump");
                        const bool lyricAnchored = waitFor([&] {
                            QQuickItem* activeLine = nullptr;
                            const int currentLyric = lyrics->property("emphasisIndex").toInt();
                            QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, activeLine), Q_ARG(int, currentLyric));
                            return activeLine && std::abs(activeLine->mapToItem(lyrics, QPointF()).y()
                                - lyrics->height() * lyrics->property("readingAnchor").toReal()) < 2;
                        }, 1500);
                        if (!lyricAnchored) {
                            qInfo() << "Unsettled lyric" << lyrics->property("emphasisIndex")
                                    << "contentY" << lyrics->property("contentY") << "viewport" << lyrics->height()
                                    << "model" << lyrics->property("model");
                        }
                        check(lyricAnchored, "current lyric stays at its reading anchor after fullscreen resizing");
                        check(shell.grabWindow().save(entering ? "/tmp/echo-detail-layout/fullscreen-entered.png"
                                                             : "/tmp/echo-detail-layout/fullscreen-exited.png"),
                            "native fullscreen layout screenshot saved");
                    };
                    verifyNativeTransition(true);
                    verifyNativeTransition(false);
                    check(shell.size() == QSize(1180, 800), "fullscreen exit restores the original window size");
                    check(qmlIssues.isEmpty(), "playback resize has no QML warnings");
                    qInfo() << "PASS" << checks << "playback resize checks";
                    QCoreApplication::exit(0);
                });
                return app.exec();
            }
            shell.resize(1920, 1050);
            pump(250);
            check(detail->findChild<QQuickItem*>("detailTitle")->property("text").toString() == playback.track().value("title").toString(),
                "playback title matches the current track");
            const auto metadata = detail->findChild<QQuickItem*>("detailMetadata")->property("text").toString();
            check(metadata.contains(playback.track().value("artist").toString()) && metadata.contains(playback.track().value("album").toString()),
                "artist and album appear above the lyrics");
            // Compare actual composited text to the same frame without its mask.
            // Background subtraction verifies transparent fading over the real gradient.
            QVariantList fadeLines;
            for (int i = 0; i < 60; ++i)
                fadeLines << QVariantMap{{"text", QStringLiteral("歌词边缘渐隐  TEXT 0123456789")}, {"time", -1}};
            lyrics->setProperty("manualScrolling", true);
            lyrics->setProperty("model", fadeLines);
            lyrics->setProperty("activeLyricIndex", -1);
            pump(100);
            lyrics->setProperty("contentY", 0);
            lyrics->setOpacity(0);
            pump(50);
            const auto backdrop = shell.grabWindow();
            lyrics->setOpacity(1);
            pump(100);
            const auto faded = shell.grabWindow();
            check(QQmlProperty::write(lyrics, "layer.enabled", false), "lyric mask can be bypassed for pixel comparison");
            pump(100);
            const auto unmasked = shell.grabWindow();
            const auto viewport = lyrics->mapRectToScene(lyrics->boundingRect());
            const auto fadeHeight = lyrics->property("edgeFadeHeight").toReal();
            const auto energyRatio = [&](qreal top, qreal bottom) {
                double maskedEnergy = 0, plainEnergy = 0;
                const qreal dpr = shell.devicePixelRatio();
                for (int y = qCeil((viewport.top() + top) * dpr); y < (viewport.top() + bottom) * dpr; ++y)
                    for (int x = qCeil(viewport.left() * dpr); x < viewport.right() * dpr; ++x) {
                        const auto bg = backdrop.pixelColor(x, y), mask = faded.pixelColor(x, y), plain = unmasked.pixelColor(x, y);
                        maskedEnergy += std::abs(mask.red() - bg.red()) + std::abs(mask.green() - bg.green()) + std::abs(mask.blue() - bg.blue());
                        plainEnergy += std::abs(plain.red() - bg.red()) + std::abs(plain.green() - bg.green()) + std::abs(plain.blue() - bg.blue());
                    }
                check(plainEnergy > 1000, "pixel comparison contains actual lyric strokes");
                return maskedEnergy / plainEnergy;
            };
            const auto topFade = energyRatio(0, fadeHeight / 2);
            const auto bodyFade = energyRatio(fadeHeight, lyrics->height() - fadeHeight);
            const auto bottomFade = energyRatio(lyrics->height() - fadeHeight / 2, lyrics->height());
            qInfo() << "Lyric fade pixel ratios top/body/bottom" << topFade << bodyFade << bottomFade;
            check(topFade < .6 && bottomFade < .6 && bodyFade > .9, "both lyric edges fade while reading area remains opaque");
            check(faded.save("/tmp/echo-detail-layout/lyric-edge-fade.png"), "actual lyric fade proof saved");
            check(QQmlProperty::write(lyrics, "layer.enabled", true), "lyric mask restored");
            lyrics->setProperty("model", lines);
            lyrics->setProperty("manualScrolling", false);
            lyrics->setProperty("activeLyricIndex", 6);
            pump(200);
            QQuickItem* activeLine = nullptr;
            check(QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, activeLine), Q_ARG(int, 6))
                    && activeLine, "active lyric is instantiated");
            const auto localLine = activeLine->mapToItem(lyrics, QPointF(0, 0));
            check(std::abs(localLine.y() - lyrics->height() * lyrics->property("readingAnchor").toReal()) < 2, "current lyric follows the reading anchor");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                activeLine->mapToScene(QPointF(40, activeLine->height() / 2)).toPoint());
            check(waitFor([&] { return playback.position() >= 6000 && playback.position() < 6100; }), "clicking a lyric still seeks playback");
            const auto point = lyrics->mapToScene(QPointF(lyrics->width() / 2, lyrics->height() / 2));
            const auto beforeScroll = lyrics->property("contentY").toReal();
            QElapsedTimer scrollClock;
            scrollClock.start();
            const auto sendWheel = [&](int delta, Qt::ScrollPhase phase) {
                QWheelEvent wheel(point, shell.mapToGlobal(point.toPoint()), QPoint(0, delta), QPoint(0, delta * 8),
                    Qt::NoButton, Qt::NoModifier, phase, false, Qt::MouseEventSynthesizedBySystem);
                wheel.setTimestamp(1 + scrollClock.elapsed());
                QCoreApplication::sendEvent(&shell, &wheel);
            };
            sendWheel(-1, Qt::ScrollBegin);
            for (int i = 0; i < 20; ++i) { sendWheel(-3, Qt::ScrollUpdate); pump(15); }
            sendWheel(0, Qt::ScrollEnd);
            qInfo() << "Lyric scroll" << beforeScroll << lyrics->property("contentY") << lyrics->property("manualScrolling");
            check(lyrics->property("manualScrolling").toBool()
                    && std::abs(lyrics->property("contentY").toReal() - beforeScroll) > 10,
                "manual lyric scrolling still moves and suspends following");
            lyrics->setProperty("manualScrolling", false);
            pump(200);
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-active.png"), "middle lyric reading layout saved");
            lyrics->setProperty("model", QVariantList{});
            pump(100);
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-empty.png"), "empty lyric layout saved");
            lyrics->setProperty("model", lines);
            lyrics->setProperty("activeLyricIndex", -1);
            pump(200);
            QQuickItem* firstLine = nullptr;
            QQuickItem* nearbyLine = nullptr;
            QQuickItem* distantLine = nullptr;
            QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, firstLine), Q_ARG(int, 0));
            QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, nearbyLine), Q_ARG(int, 1));
            QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, distantLine), Q_ARG(int, 6));
            check(firstLine && nearbyLine && distantLine && firstLine->opacity() == 1
                    && nearbyLine->opacity() < .6 && distantLine->opacity() < .1,
                "lyrics before the first timestamp retain strong near-to-far fading");
            check(std::abs(firstLine->mapToItem(lyrics, QPointF(0, 0)).y()
                    - lyrics->height() * lyrics->property("readingAnchor").toReal()) < 2,
                "first lyric uses the reading anchor before playback reaches its timestamp");
            // Exercise the actual shared control bar, including passive pointer
            // observation over controls and the real three-second idle timeout.
            auto* bar = shell.rootObject()->findChild<QQuickItem*>("playerBar");
            auto* hideTimer = shell.rootObject()->findChild<QObject*>("controlsHideTimer");
            auto* seek = bar->findChild<QQuickItem*>("barSeek");
            auto* collapse = detail->findChild<QQuickItem*>("detailCollapse");
            check(bar && hideTimer && seek && collapse && hideTimer->property("interval").toInt() == 3000,
                "playback controls use a three-second idle timeout");
            shell.resize(1180, 800);
            lyrics->setProperty("model", lines);
            lyrics->setProperty("activeLyricIndex", 6);
            shell.rootObject()->setProperty("expanded", false);
            shell.rootObject()->forceActiveFocus();
            client.setReduceMotion(false);
            player.setRepeat(echo::Player::Repeat::One);
            playback.seek(0);
            playback.toggle();
            check(waitFor([&] { return playback.playing(); }), "idle fixture plays");
            const auto fixedBarPosition = bar->mapToScene(QPointF(0, 0));
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, bar->mapToScene(QPointF(10, 70)).toPoint());
            pump(80);
            check(detail->y() > 0 && detail->y() < shell.height() - bar->height()
                    && bar->isVisible() && bar->isEnabled() && bar->mapToScene(QPointF(0, 0)) == fixedBarPosition,
                "detail rises from behind the stationary interactive control bar");
            check(detail->parentItem()->clip() && detail->parentItem()->height() == shell.height() - bar->height(),
                "entering detail is clipped at the top of the shared bar");
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-entering.png"), "shared bar transition frame saved");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(28, 28)).toPoint());
            check(waitFor([&] { return !playback.playing(); }), "shared transport remains usable during page transition");
            playback.toggle();
            pump(450);
            const auto lyricPoint = lyrics->mapToScene(QPointF(100, lyrics->height() / 2)).toPoint();
            QEnterEvent enter(lyricPoint, lyricPoint, shell.mapToGlobal(lyricPoint));
            QCoreApplication::sendEvent(&shell, &enter);
            QTest::mouseMove(&shell, lyricPoint);
            const auto coverBeforeHide = cover->mapRectToScene(cover->boundingRect());
            pump(700);
            check(bar->isVisible() && bar->opacity() == 1, "shared controls remain visible during initial listening period");
            const bool didHide = waitFor([&] { return !bar->isVisible(); }, 4000);
            check(didHide, "playing without pointer activity animates controls away");
            check(!bar->isEnabled() && bar->y() >= shell.height(), "hidden controls have no invisible hit targets");
            check(!collapse->isVisible() && !collapse->isEnabled(), "collapse arrow hides and disables with the shared controls");
            check(cover->mapRectToScene(cover->boundingRect()) == coverBeforeHide, "hiding controls never shifts the cover or lyrics");
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-controls-hidden.png"), "immersive listening screenshot saved");
            QTest::mouseMove(&shell, lyricPoint + QPoint(10, 0));
            pump(70);
            check(bar->isVisible() && bar->opacity() > 0 && bar->opacity() < 1 && bar->y() > fixedBarPosition.y(),
                "pointer movement over lyrics animates the same bar back into view");
            check(collapse->isVisible() && std::abs(collapse->opacity() - bar->opacity()) < .001,
                "collapse arrow fades back in with the shared controls");
            pump(260);
            check(bar->isEnabled() && bar->mapToScene(QPointF(0, 0)) == fixedBarPosition,
                "pointer wake restores controls at the original position");
            // Shorter timeout for interaction permutations after testing the real delay.
            hideTimer->setProperty("interval", 300);
            QTest::mouseMove(&shell, bar->mapToScene(QPointF(10, 70)).toPoint());
            button->forceActiveFocus(Qt::TabFocusReason);
            check(waitFor([&] { return !bar->isVisible(); }, 1500),
                "stationary pointer and retained keyboard focus do not block auto-hide");
            auto* volume = bar->findChild<QQuickItem*>("barVolume");
            const auto volumePoint = (fixedBarPosition + volume->mapToItem(bar, QPointF(22, 22))).toPoint();
            QTest::mouseMove(&shell, volumePoint);
            check(waitFor([&] { return bar->isVisible() && bar->opacity() == 1; }), "pointer over hidden controls wakes the bar");
            auto* volumePopup = bar->findChild<QObject*>("barVolumePopup");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, volume->mapToScene(QPointF(22, 22)).toPoint());
            check(waitFor([&] { return volumePopup->property("opened").toBool(); }), "shared volume popup opens from playback page");
            QTest::mouseMove(&shell, lyricPoint);
            pump(700);
            check(bar->isVisible() && bar->opacity() == 1, "open volume popup prevents auto-hide");
            QMetaObject::invokeMethod(volumePopup, "close");
            check(waitFor([&] { return !bar->isVisible(); }, 1500), "auto-hide resumes after volume popup closes");
            QTest::keyClick(&shell, Qt::Key_Space);
            check(waitFor([&] { return !playback.playing() && bar->opacity() == 1; }), "keyboard pause brings hidden controls back");
            pump(700);
            check(!bar->isVisible() && !collapse->isVisible(), "paused playback also hides idle controls and collapse arrow");
            client.setReduceMotion(true);
            playback.toggle();
            check(waitFor([&] { return !bar->isVisible(); }, 1500), "reduced motion still hides idle controls");
            QTest::mouseMove(&shell, lyricPoint + QPoint(20, 0));
            check(waitFor([&] { return bar->isVisible() && bar->opacity() == 1; }), "reduced motion wakes controls without intermediate opacity");
            const auto seekPoint = seek->mapToScene(QPointF(seek->width() * .4, seek->height() / 2)).toPoint();
            QTest::mousePress(&shell, Qt::LeftButton, Qt::NoModifier, seekPoint);
            pump(700);
            check(seek->property("pressed").toBool() && bar->isVisible(), "held seek interaction prevents auto-hide");
            QTest::mouseMove(&shell, seekPoint + QPoint(80, 0));
            QTest::mouseRelease(&shell, Qt::LeftButton, Qt::NoModifier, seekPoint + QPoint(80, 0));
            check(waitFor([&] { return !seek->property("pressed").toBool() && playback.position() > 7000; }),
                "shared progress slider still seeks after pointer wake");
            QTest::mouseMove(&shell, lyricPoint);
            check(waitFor([&] { return !bar->isVisible(); }, 1500), "controls hide again after seeking");
            QTest::keyClick(&shell, Qt::Key_Escape);
            check(!detail->isVisible() && bar->isVisible() && bar->isEnabled() && bar->mapToScene(QPointF(0, 0)) == fixedBarPosition,
                "leaving immersive playback immediately restores library controls");
            pump(700);
            check(bar->isVisible(), "main library never auto-hides the shared bar");
            // The queue floats over playback without navigating or reflowing it.
            player.playTracks({ids[0], otherIds[0], otherIds[1]}, 0, false);
            check(waitFor([&] { return playback.playing() && client.queue()->count() == 3; }), "floating queue fixture loads");
            playback.toggle();
            check(waitFor([&] { return !playback.playing(); }), "floating queue fixture pauses");
            client.setReduceMotion(false);
            shell.rootObject()->setProperty("expanded", true);
            pump(450);
            auto* queueToggle = bar->findChild<QQuickItem*>("queueToggle");
            auto* floatingQueue = shell.rootObject()->findChild<QQuickItem*>("playbackQueuePanel");
            auto* floatingList = shell.rootObject()->findChild<QQuickItem*>("playbackQueueList");
            auto* libraryQueue = shell.rootObject()->findChild<QQuickItem*>("queuePanel");
            const auto coverBeforeQueue = cover->mapRectToScene(cover->boundingRect());
            const auto lyricsBeforeQueue = lyrics->mapRectToScene(lyrics->boundingRect());
            const auto clickQueue = [&] {
                QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                    queueToggle->mapToScene(QPointF(22, 22)).toPoint());
            };
            clickQueue();
            pump(80);
            check(shell.rootObject()->property("expanded").toBool() && shell.rootObject()->property("queueOpen").toBool(),
                "queue button opens a floating queue without leaving playback");
            check(floatingQueue && floatingList && floatingQueue->property("openness").toReal() > 0
                    && floatingQueue->property("openness").toReal() < 1,
                "playback queue slides in from the right");
            check(waitFor([&] { return floatingQueue->property("openness").toReal() == 1; }), "playback queue finishes opening");
            const auto queueBounds = floatingQueue->mapRectToScene(floatingQueue->boundingRect());
            check(queueBounds.right() == shell.width() - 12 && queueBounds.top() >= 48
                    && queueBounds.bottom() <= fixedBarPosition.y() && floatingQueue->width() == 320,
                "floating queue fits between titlebar and shared controls");
            check(cover->mapRectToScene(cover->boundingRect()) == coverBeforeQueue
                    && lyrics->mapRectToScene(lyrics->boundingRect()) == lyricsBeforeQueue && !libraryQueue->isVisible(),
                "playback cover and lyrics retain their layout while library queue stays hidden");
            pump(750);
            check(bar->opacity() == 1 && collapse->opacity() == 1,
                "open queue keeps playback controls and collapse arrow visible beyond idle timeout");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                floatingList->mapToScene(QPointF(100, floatingList->height() - 15)).toPoint());
            check(shell.rootObject()->property("queueOpen").toBool(), "empty space inside floating queue never dismisses it");
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-queue.png"), "floating playback queue screenshot saved");
            QTest::mouseDClick(&shell, Qt::LeftButton, Qt::NoModifier,
                floatingList->mapToScene(QPointF(100, 92)).toPoint());
            check(waitFor([&] { return player.currentTrack().id == otherIds[0]; }), "floating queue double-click switches the real track");
            check(shell.rootObject()->property("expanded").toBool() && shell.rootObject()->property("queueOpen").toBool()
                    && detail->findChild<QQuickItem*>("detailTitle")->property("text").toString() == playback.track().value("title").toString(),
                "switching tracks updates playback detail and leaves the queue open");
            check(waitFor([&] { return playback.playing(); }), "queue selection starts playback after async track loading");
            playback.toggle();
            check(waitFor([&] { return !playback.playing(); }), "queue selection pauses for outside-click check");
            pump(350); // Let the transport pause fade finish before comparing positions.
            const auto positionBeforeDismiss = playback.position();
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                lyrics->mapToScene(QPointF(40, lyrics->height() * .42)).toPoint());
            check(waitFor([&] { return floatingQueue->property("openness").toReal() == 0; }), "outside lyric-area click dismisses the queue");
            qInfo() << "Queue dismiss position" << positionBeforeDismiss << playback.position() << playback.playing();
            check(shell.rootObject()->property("expanded").toBool() && std::abs(playback.position() - positionBeforeDismiss) < 40,
                "outside dismissal stays in playback and never seeks the underlying lyric");
            pump(100);
            check(bar->isVisible(), "queue dismissal gives controls a fresh idle interval");
            check(waitFor([&] { return !bar->isVisible() && !collapse->isVisible(); }, 1500), "idle hiding resumes after queue closes");
            QTest::mouseMove(&shell, lyricPoint + QPoint(30, 0));
            check(waitFor([&] { return bar->opacity() == 1; }), "controls wake before reopening queue");
            clickQueue();
            check(waitFor([&] { return floatingQueue->property("openness").toReal() == 1; }), "floating queue reopens");
            QTest::keyClick(&shell, Qt::Key_Escape);
            check(shell.rootObject()->property("expanded").toBool() && !shell.rootObject()->property("queueOpen").toBool(),
                "first Escape closes the queue and preserves playback page");
            QTest::keyClick(&shell, Qt::Key_Escape);
            check(!shell.rootObject()->property("expanded").toBool(), "second Escape exits playback after queue closes");
            pump(450);
            client.setReduceMotion(true);
            shell.rootObject()->setProperty("expanded", true);
            clickQueue();
            check(floatingQueue->isVisible() && floatingQueue->property("openness").toReal() == 1,
                "reduced motion opens floating queue immediately");
            clickQueue();
            check(!floatingQueue->isVisible() && shell.rootObject()->property("expanded").toBool(),
                "same queue button closes the floating panel without leaving playback");
            clickQueue();
            auto* closeQueue = floatingQueue->findChild<QQuickItem*>("queueCloseButton");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, closeQueue->mapToScene(QPointF(15, 15)).toPoint());
            check(!floatingQueue->isVisible() && shell.rootObject()->property("expanded").toBool(),
                "floating queue close button preserves playback page");
            shell.rootObject()->setProperty("expanded", false);
            check(qmlIssues.isEmpty(), "detail layout has no QML warnings");
            qInfo() << "PASS" << checks << "detail layout checks";
            return 0;
        }
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "native Quick window exposed");
        player.playTracks(ids, 0, false);
        check(waitFor([&] { return playback.playing() && playback.position() > 150 && probe->nonzero > 0; }),
            "real decoder delivers nonzero PCM");
        QSignalSpy artworkRefresh(&playback, &echo::PlaybackView::trackChanged);
        check(QFile::remove(coverPath), "clear active track cover cache");
        check(playback.track().value("artwork").toString().isEmpty(), "missing cover selects default immediately");
        library.refreshRoot(sourceId);
        check(waitFor([&] { return !library.scanBusy() && QFile::exists(coverPath); }), "active cover cache repaired");
        check(artworkRefresh.count() > 0 && !playback.track().value("artwork").toString().isEmpty(),
            "active cover refreshes after repair even when metadata hash stays unchanged");
        check(playback.lyrics().size() == 5, "LRC exposed");
        check(waitFor([&] { return playback.lyricIndex() == 1; }), "lyrics follow playback clock");
        auto* toggle = window.rootObject()->findChild<QQuickItem*>("barToggle");
        check(toggle, "playback control exists");
        check(QRectF(0, 0, window.width(), window.height())
                  .contains(toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2))),
            "playback control is inside window");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
            toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        check(waitFor([&] { return !playback.playing(); }), "actual QML button pauses audio");
        playback.toggle();
        check(waitFor([&] { return playback.playing(); }), "resume works");
        playback.seek(4500);
        check(waitFor([&] { return playback.position() >= 4500 && playback.lyricIndex() == 3; }),
            "seek updates audio and lyrics");
        QSignalSpy lyricChanges(&playback, &echo::PlaybackView::lyricsChanged);
        playback.toggleFavorite();
        check(library.isFavorite(ids[0]), "favorite reaches library");
        check(lyricChanges.count() == 0, "favorite never resets lyric model");
        auto* lyrics = window.rootObject()->findChild<QQuickItem*>("lyricsView");
        check(lyrics, "lyrics scroll view exists");
        playback.toggle();
        check(waitFor([&] { return !playback.playing(); }), "pause clock for trackpad regression");
        QVariantList longLyrics;
        for (int i = 0; i < 40; ++i)
            longLyrics << QVariantMap { { "text", QString("Lyric line %1").arg(i) }, { "time", i * 1000 } };
        lyrics->setProperty("model", longLyrics);
        pump(500);
        const QPointF lyricPoint = lyrics->mapToScene(QPointF(lyrics->width() / 2, lyrics->height() / 2));
        QElapsedTimer lyricClock;
        lyricClock.start();
        auto lyricWheel = [&](int delta, Qt::ScrollPhase phase) {
            QWheelEvent event(lyricPoint, window.mapToGlobal(lyricPoint.toPoint()), QPoint(0, delta), QPoint(0, delta * 8),
                Qt::NoButton, Qt::NoModifier, phase, false, Qt::MouseEventSynthesizedBySystem);
            event.setTimestamp(1 + lyricClock.elapsed());
            QCoreApplication::sendEvent(&window, &event);
        };
        lyrics->setProperty("contentY", 300);
        lyricWheel(-1, Qt::ScrollBegin);
        pump(20);
        check(lyrics->property("manualScrolling").toBool(), "first trackpad event suspends lyric following");
        bool scrollContinuous = true, followsClock = true, staysManual = true;
        qreal lyricTravel = 0;
        // Keep a gesture active beyond the old five-second timeout, with
        // direction changes and playback index changes during manual scrolling.
        for (int i = 0; i < 440; ++i) {
            if (i == 80)
                playback.seek(7500);
            const qreal beforeScroll = lyrics->property("contentY").toReal();
            const int delta = (i / 20) % 2 ? 2 : -2;
            lyricWheel(delta, i > 400 ? Qt::ScrollMomentum : Qt::ScrollUpdate);
            pump(13);
            const qreal moved = lyrics->property("contentY").toReal() - beforeScroll;
            lyricTravel += std::abs(moved);
            if (std::abs(moved) > 20 || moved * delta > 0)
                qInfo() << "Lyric discontinuity" << i << beforeScroll << moved << delta
                        << lyrics->property("manualScrolling") << lyrics->property("activeLyricIndex");
            scrollContinuous &= std::abs(moved) <= 20 && moved * delta <= 0;
            followsClock &= lyrics->property("activeLyricIndex").toInt() == playback.lyricIndex();
            staysManual &= lyrics->property("manualScrolling").toBool();
        }
        lyricWheel(0, Qt::ScrollEnd);
        check(scrollContinuous, "trackpad lyric updates and reversals never jump or pull against input");
        check(lyricTravel > 300, "trackpad events actually move the lyric viewport");
        check(followsClock, "manual lyric scrolling never changes the playback highlight index");
        check(staysManual, "long gestures and momentum remain in manual lyric mode");
        const qreal manualY = lyrics->property("contentY").toReal();
        pump(700);
        check(std::abs(lyrics->property("contentY").toReal() - manualY) < 1,
            "lyrics stay where released while automatic following is suspended");
        check(waitFor([&] { return !lyrics->property("manualScrolling").toBool(); }, 6000),
            "lyric following resumes only after idle timeout");
        pump(500);
        check(lyrics->property("activeLyricIndex").toInt() == playback.lyricIndex(),
            "idle resume retains current playback lyric");
        check(std::abs(lyrics->property("contentY").toReal() - manualY) > 30,
            "idle resume scrolls back to the playback lyric");
        lyrics->setProperty("model", playback.lyrics());
        playback.seek(4500);
        playback.toggle();
        const int before = probe->blocks;
        pump(500);
        QVector<double> intervals;
        QElapsedTimer frameClock;
        frameClock.start();
        qint64 previous = 0;
        const auto frameConnection = QObject::connect(&window, &QQuickWindow::frameSwapped, &app, [&] {
            const qint64 now = frameClock.nsecsElapsed();
            if (previous)
                intervals << (now - previous) / 1e6;
            previous = now;
        });
        for (int i = 0; i < 24; ++i) {
            window.rootObject()->setProperty("expanded", i % 2 == 1);
            pump(70);
        }
        QObject::disconnect(frameConnection); // Idle windows intentionally stop rendering; don't count idle gaps as
                                              // dropped animation frames.
        check(probe->blocks > before + 100, "audio continues during interrupted transitions");
        check(window.rootObject()->property("expanded").toBool(), "transition reversal ends in requested state");
        pump(500);
        QDir().mkpath("/tmp/echo-quick-qa");
        const auto screenshot = window.grabWindow();
        check(screenshot.save("/tmp/echo-quick-qa/prototype.png"), "GPU screenshot saved");
        check(screenshot.pixelColor(int(250 * window.devicePixelRatio()), int(250 * window.devicePixelRatio()))
                    .saturationF()
                > .15,
            "actual artwork renders through rounded mask");
        auto* renderedCover = window.rootObject()->findChild<QQuickItem*>("detailCover");
        window.raise();
        window.requestActivate();
        window.requestUpdate();
        auto coverGrab = renderedCover->grabToImage(QSize(240, 240));
        check(waitFor([&] { return !coverGrab->image().isNull(); }), "rounded cover captured");
        const auto coverImage = coverGrab->image();
        int partialEdgePixels = 0;
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 18; ++x) {
                const int alpha = coverImage.pixelColor(x, y).alpha();
                partialEdgePixels += alpha > 0 && alpha < 255;
            }
        check(coverImage.pixelColor(0, 0).alpha() == 0 && partialEdgePixels >= 4,
            "cover corners preserve transparent antialiased edge pixels");
        check(coverImage.save("/tmp/echo-quick-qa/cover.png"), "cover edge proof saved");
        check(intervals.size() > 40, "animated frames presented");
        if (!intervals.isEmpty()) {
            std::sort(intervals.begin(), intervals.end());
            qInfo() << "Active animation frame intervals ms median/p95/max" << intervals[intervals.size() / 2]
                    << intervals[intervals.size() * 95 / 100] << intervals.last();
            check(intervals[intervals.size() * 95 / 100] < 25, "60 Hz animation p95 under 25 ms");
        }
        qInfo() << "Graphics API" << window.rendererInterface()->graphicsApi() << "DPR" << window.devicePixelRatio();
        window.hide();
        echo::Client client(&library, &player, &engine);
        echo::QuickView shell(&playback, &client);
        check(shell.status() == QQuickView::Ready, "migrated shell QML loads");
        client.attach(&shell);
        shell.show();
        check(QTest::qWaitForWindowExposed(&shell), "migrated shell exposed");
        check(shell.width() >= shell.minimumWidth() && shell.height() >= shell.minimumHeight(),
            "custom window respects minimum size at current scale");
        check(waitFor([&] { return !client.page().value("busy").toBool(); }), "asynchronous library query completes");
        check(client.tracks()->count() == 3, "registered tracks visible in new library");
        check(!client.preferences().value("dynamicPlaybackBackground").toBool(),
            "dynamic playback background defaults to disabled");
        {
            auto object = [&](const char* name) { return shell.rootObject()->findChild<QObject*>(name); };
            auto* settings = object("settingsDialog");
            auto* detail = shell.rootObject()->findChild<QQuickItem*>("nowPlaying");
            client.showSettings();
            check(waitFor([&] { return settings->property("opened").toBool(); }), "background settings opens");
            auto* backgroundSetting = qobject_cast<QQuickItem*>(object("dynamicBackgroundSetting"));
            check(backgroundSetting && !backgroundSetting->property("checked").toBool(),
                "settings exposes an unchecked dynamic background option by default");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                backgroundSetting->mapToScene(QPointF(12, backgroundSetting->height() / 2)).toPoint());
            check(client.preferences().value("dynamicPlaybackBackground").toBool()
                    && library.setting("dynamic_playback_background") == "1" && detail->property("dynamicBackground").toBool(),
                "dynamic background checkbox applies immediately and persists its value");
            QMetaObject::invokeMethod(settings, "close");
            check(waitFor([&] { return !settings->property("visible").toBool(); }), "background settings closes");
            shell.rootObject()->setProperty("expanded", true);
            pump(550);
            const auto backgroundPoint = detail->mapToScene(QPointF(20, 100)) * shell.devicePixelRatio();
            const auto dynamicImage = shell.grabWindow();
            check(dynamicImage.pixelColor(backgroundPoint.toPoint()) != QColor("#16141d"),
                "enabled playback background uses the current artwork");
            client.setPreference("dynamicPlaybackBackground", false);
            pump(100);
            const auto staticImage = shell.grabWindow();
            check(detail->findChild<QObject*>("playbackBackgroundImage")->property("source").toUrl()
                        == QUrl("image://echo-icons/cover/background")
                    && staticImage.pixelColor(backgroundPoint.toPoint()) != QColor("#16141d")
                    && library.setting("dynamic_playback_background") == "0",
                "disabled playback background uses the fixed default cover gradient");
            shell.rootObject()->setProperty("expanded", false);
            pump(450);
            auto* provider = static_cast<QQuickImageProvider*>(shell.engine()->imageProvider("echo-icons"));
            QSize backdropSize;
            const auto plainBackdrop = provider->requestPixmap("cover/background", &backdropSize, QSize(800, 800)).toImage();
            const auto labeledCover = provider->requestPixmap("cover/light", &backdropSize, QSize(800, 800)).toImage();
            int largestColorStep = 0;
            for (int y = 0; y < plainBackdrop.height(); ++y)
                for (int x = 1; x < plainBackdrop.width(); ++x) {
                    const auto previous = plainBackdrop.pixelColor(x - 1, y);
                    const auto current = plainBackdrop.pixelColor(x, y);
                    largestColorStep = std::max({largestColorStep, qAbs(previous.red() - current.red()),
                        qAbs(previous.green() - current.green()), qAbs(previous.blue() - current.blue())});
                }
            check(largestColorStep <= 2 && labeledCover != plainBackdrop,
                "default backdrop is a continuous gradient while the cover retains its lettering");
            client.showMini();
            auto* mini = shell.rootObject()->findChild<QQuickWindow*>("miniPlayer");
            check(mini && waitFor([&] { return mini->isVisible(); }), "mini player opens for alignment regression");
            pump(100);
            bool alignedMiniButtons = true;
            for (const char* name : {"miniFavorite", "miniPrevious", "miniToggle", "miniNext", "miniReturn"}) {
                const auto* button = mini->findChild<QQuickItem*>(name);
                alignedMiniButtons &= button && qAbs(button->mapToScene(QPointF(0, button->height() / 2)).y() - 41) < .5
                    && button->width() == button->height();
            }
            check(alignedMiniButtons, "all five mini transport buttons share a vertical center and square bounds");
            client.showMainWindow();
            check(waitFor([&] { return shell.isVisible() && !mini->isVisible(); }), "return to main after alignment check");
        }
        if (app.arguments().contains("--background-test")) {
            check(qmlIssues.isEmpty(), "background settings produce no QML warnings");
            qInfo() << "PASS" << checks << "focused playback background checks";
            return 0;
        }
        bool playlistSection = false;
        for (int i = 0; i < client.navigation()->count(); ++i)
            playlistSection |= client.navigation()->get(i).value("kind") == "playlistSection";
        check(playlistSection, "playlist section exists even before creating a user playlist");
        const auto favoriteNavigation = client.navigation()->get(5);
        check(client.navigation()->get(4).value("kind") == "playlistSection"
                && favoriteNavigation.value("key").toLongLong() == library.favoritesPlaylistId()
                && favoriteNavigation.value("protected").toBool(),
            "favorites is the fixed first entry under the playlist section");
        library.renamePlaylist(library.favoritesPlaylistId(), "Changed");
        library.deletePlaylist(library.favoritesPlaylistId());
        check(client.navigation()->get(5) == favoriteNavigation
                && library.favoritesPlaylistId() == favoriteNavigation.value("key").toLongLong(),
            "favorites cannot be renamed or deleted");
        check(shell.rootObject()->findChild<QQuickItem*>("backButton")->property("filled").toBool(),
            "back button has a persistent circular background");
        auto* caption = shell.rootObject()->findChild<QQuickItem*>("windowBar");
        check(caption && caption->height() == 48 && caption->width() == shell.width(),
            "integrated caption fills window width");
        check(shell.frameMargins().top() == 0, "system title bar reserves no content space");
        auto checkHint = [&](QQuickItem* owner, const QString& expected, const char* screenshotName) {
            shell.raise(); shell.requestActivate();
            check(waitFor([&] { return shell.isActive(); }), "tooltip owner window is active");
            // QTest moves bypass Cocoa; establish the enter event normally supplied by QPA.
            const auto localPointer = owner->mapToScene(QPointF(owner->width()/2, owner->height()/2));
            QEnterEvent enter(localPointer, localPointer, shell.mapToGlobal(localPointer));
            QCoreApplication::sendEvent(&shell, &enter);
            auto* hint = owner->findChild<QObject*>("echoToolTip");
            check(hint, "button uses shared tooltip component");
            QTest::mouseMove(&shell, QPoint(100, 440));
            pump(100);
            QTest::mouseMove(&shell, owner->mapToScene(QPointF(owner->width() / 2, owner->height() / 2)).toPoint());
            pump(150);
            check(!hint->property("opened").toBool(), "tooltip does not flash on brief hover");
            const bool tooltipOpened = waitFor([&] {
                QTest::mouseMove(&shell, owner->mapToScene(QPointF(owner->width()/2, owner->height()/2)).toPoint());
                return hint->property("opened").toBool();
            });
            if (!tooltipOpened) qInfo() << "Tooltip diagnostics" << owner->objectName() << owner->property("hovered")
                << hint->property("visible") << hint->property("text") << shell.isActive() << owner->mapToScene(QPointF());
            check(tooltipOpened, "tooltip opens after hover delay");
            check(hint->property("text").toString() == expected, "tooltip text matches current action");
            auto* background = qvariant_cast<QQuickItem*>(hint->property("background"));
            check(background, "tooltip has custom rounded background");
            const auto bounds = background->mapRectToScene(background->boundingRect());
            check(bounds.left() >= 0 && bounds.top() >= 0 && bounds.right() <= shell.width()
                    && bounds.bottom() <= shell.height(), "tooltip stays inside window at screen edges");
            const auto frame = shell.grabWindow();
            const QPointF sample(bounds.left() + 4, bounds.center().y());
            check(frame.pixelColor(qRound(sample.x() * shell.devicePixelRatio()),
                      qRound(sample.y() * shell.devicePixelRatio())) == QColor("#302938"),
                "rendered tooltip uses shared dark palette");
            check(frame.save(QString("/tmp/echo-quick-qa/") + screenshotName), "tooltip screenshot saved");
            QTest::mouseMove(&shell, QPoint(100, 440));
            check(waitFor([&] { return !hint->property("visible").toBool(); }), "tooltip closes on pointer exit");
        };
        auto* favoriteHintButton = shell.rootObject()->findChild<QQuickItem*>("barFavorite");
        checkHint(favoriteHintButton, favoriteHintButton->property("label").toString(), "tooltip.png");
        const auto realPlatform = caption->property("platformName");
        for (const QString& platform : { QStringLiteral("windows"), QStringLiteral("linux") }) {
            caption->setProperty("platformName", platform);
            pump(50);
            auto* minimize = caption->findChild<QQuickItem*>("windowMinimize");
            auto* maximize = caption->findChild<QQuickItem*>("windowMaximize");
            auto* close = caption->findChild<QQuickItem*>("windowClose");
            check(
                minimize && maximize && close && close->isVisible(), "Windows/Linux caption exposes all three actions");
            check(close->mapToScene(QPointF(close->width(), 0)).x() == shell.width() - 8,
                "Windows/Linux controls align at right edge");
            if (platform == "windows")
                checkHint(close, QStringLiteral("关闭窗口"), "tooltip-window.png");
            const auto restoredSize = shell.size();
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, maximize->mapToScene(QPointF(22, 20)).toPoint());
            check(waitFor([&] { return shell.maximized(); }), "custom maximize action reaches OS window state");
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, maximize->mapToScene(QPointF(22, 20)).toPoint());
            check(waitFor([&] { return !shell.maximized(); }), "custom restore action restores window state");
            shell.resize(restoredSize);
            pump(100);
            auto* dragArea = caption->findChild<QQuickItem*>("windowDragArea");
            check(dragArea, "caption exposes a separate drag region");
            const auto captionCenter = dragArea->mapToScene(QPointF(dragArea->width() / 2, 12)).toPoint();
            QTest::mouseDClick(&shell, Qt::LeftButton, Qt::NoModifier, captionCenter);
            check(waitFor([&] { return shell.maximized(); }), "caption double click maximizes");
            QTest::mouseDClick(&shell, Qt::LeftButton, Qt::NoModifier, captionCenter);
            check(waitFor([&] { return !shell.maximized(); }), "caption double click restores");
            shell.resize(restoredSize);
            pump(100);
            QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, minimize->mapToScene(QPointF(22, 20)).toPoint());
            check(waitFor([&] { return shell.visibility() == QWindow::Minimized; }),
                "custom minimize action reaches OS window state");
            client.showMainWindow();
            check(waitFor([&] { return shell.visibility() == QWindow::Windowed; }),
                "resident action restores minimized custom window");
        }
        caption->setProperty("platformName", realPlatform);
        pump(100);
        const auto sizeBeforeFullscreen = shell.size();
        shell.showFullScreen();
        check(waitFor([&] { return shell.fullscreen(); }), "custom chrome enters fullscreen");
        pump(600);
        shell.showNormal();
        check(waitFor([&] { return shell.visibility() == QWindow::Windowed; }), "custom chrome leaves fullscreen");
        pump(600);
        check(shell.frameMargins().top() == 0, "leaving fullscreen does not restore system title bar");
        shell.resize(sizeBeforeFullscreen);

        pump(400);
        check(shell.size() == sizeBeforeFullscreen, "fullscreen restores logical window size at current scale");
        check(shell.grabWindow().save("/tmp/echo-quick-qa/library.png"), "library screenshot saved");
        auto* backButton = shell.rootObject()->findChild<QQuickItem*>("backButton");
        auto* searchHeader = shell.rootObject()->findChild<QQuickItem*>("searchField");
        check(backButton->width() == backButton->height()
                && qAbs(backButton->mapToScene(QPointF(0, backButton->height() / 2)).y()
                    - searchHeader->mapToScene(QPointF(0, searchHeader->height() / 2)).y()) < 0.5,
            "back button is circular and aligned with search field");
        auto* pageContent = shell.rootObject()->findChild<QQuickItem*>("pageContent");
        client.navigate("albums", {}, QStringLiteral("专辑"));
        check(client.tracks()->count() == 3, "navigation retains old rows while fading out");
        pump(50);
        check(pageContent->opacity() > 0 && pageContent->opacity() < 1 && !pageContent->isEnabled(),
            "outgoing page fades smoothly and blocks stale interactions");
        check(waitFor([&] { return !client.page().value("busy").toBool() && pageContent->opacity() == 1; }),
            "replacement page fades in after atomic model update");
        client.back();
        check(waitFor([&] { return !client.page().value("busy").toBool() && pageContent->opacity() == 1; }),
            "back navigation restores complete page through transition");
        auto* list = shell.rootObject()->findChild<QQuickItem*>("trackList");
        check(list, "native track view exists");
        auto clickRow = [&](int row, Qt::KeyboardModifiers mods = Qt::NoModifier, int x = 160) {
            QTest::mouseClick(&shell, Qt::LeftButton, mods, list->mapToScene(QPointF(x, 24 + row * 48)).toPoint());
            pump(30);
        };
        clickRow(0);
        check(client.selectedCount() == 1 && client.isSelected(0), "actual row click selects");
        clickRow(1, Qt::ShiftModifier);
        check(client.selectedCount() == 2, "shift range selection works through QML");
        const auto favoriteId = client.tracks()->get(0).value("id").toLongLong();
        const bool wasLiked = library.isFavorite(favoriteId);
        clickRow(0, Qt::NoModifier, 23);
        check(library.isFavorite(favoriteId) != wasLiked, "actual row heart toggles favorite");
        check(client.selectedCount() == 2, "heart preserves multi-selection");
        QTest::keyClick(&shell, Qt::Key_Space);
        check(waitFor([&] { return !playback.playing(); }), "space pauses from track view");
        QTest::keyClick(&shell, Qt::Key_Space);
        check(waitFor([&] { return playback.playing(); }), "space resumes from track view");
        auto* search = shell.rootObject()->findChild<QQuickItem*>("searchField");
        check(search, "search field exists");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, search->mapToScene(QPointF(80, 15)).toPoint());
        QTest::keyClick(&shell, Qt::Key_Z);
        QTest::keyClick(&shell, Qt::Key_Z);
        QTest::keyClick(&shell, Qt::Key_Space);
        check(playback.playing(), "typing space in search never toggles playback");
        check(waitFor([&] { return client.page().value("kind") == "search" && !client.page().value("busy").toBool(); }),
            "typed search completes");
        qInfo() << "Search input/query/count" << search->property("text") << client.page().value("query") << client.tracks()->count();
        check(client.tracks()->count() == 0, "unmatched search has no stale tracks");
        client.search("Nightfall");
        client.navigate("albums", {}, QStringLiteral("专辑"));
        check(waitFor([&] { return !client.page().value("busy").toBool() && client.cards()->count() == 2; }),
            "navigation discards in-flight search results");
        auto* grid = shell.rootObject()->findChild<QQuickItem*>("cardGrid");
        check(grid, "album grid exists");
        pump(150);
        check(grid->height() > 100, "album grid laid out before pointer interaction");
        // Exercise column thresholds with enough cards to wrap, without touching the library.
        QVector<QVariantMap> originalCards;
        for (int i = 0; i < client.cards()->count(); ++i) originalCards << client.cards()->get(i);
        QVector<QVariantMap> layoutCards;
        for (int i = 0; i < 20; ++i) {
            auto row = originalCards[i % originalCards.size()];
            row["key"] = QString("queue-layout-fixture-%1").arg(i);
            layoutCards << row;
        }
        client.cards()->replace(layoutCards);
        std::function<QQuickItem*(QQuickItem*, const QString&)> findGridItem = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* found = findGridItem(child, name)) return found;
            return nullptr;
        };
        QVector<double> reflowFrames;
        QElapsedTimer reflowFrameClock;
        reflowFrameClock.start();
        qint64 previousReflowFrame = 0;
        const auto reflowConnection = QObject::connect(&shell, &QQuickWindow::frameSwapped, &app, [&] {
            const auto now = reflowFrameClock.nsecsElapsed();
            if (grid->property("animateQueueLayout").toBool()) {
                if (previousReflowFrame) reflowFrames << (now-previousReflowFrame)/1e6;
                previousReflowFrame = now;
            } else previousReflowFrame = 0;
        });
        const auto beforeGridResize = shell.size();
        auto* layoutQueueToggle = shell.rootObject()->findChild<QQuickItem*>("queueToggle");
        for (const int viewportWidth : {1180, 1040}) {
            shell.resize(viewportWidth, 800);
            grid->setProperty("contentY", 0);
            pump(350);
            QPointer<QQuickItem> cover = findGridItem(grid, "cardArtwork2");
            check(cover, "third album remains instantiated for queue reflow regression");
            const auto originalDecode = cover->property("decodeSize");
            const auto originalTexture = cover->property("renderTextureSize");
            for (const bool opening : {true, false}) {
                const QRectF before = cover->mapRectToScene(cover->boundingRect());
                const qreal available = viewportWidth - 218 - 60 - (opening ? 270 : 0);
                const int columns = qMax(2, int(available / 188));
                const qreal cell = available / columns;
                const QPointF origin = grid->mapToScene(QPointF(0, 0));
                const QRectF destination(origin.x() + (2 % columns) * cell,
                    origin.y() + (2 / columns) * (cell + 40), cell - 18, cell - 18);
                QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                    layoutQueueToggle->mapToScene(QPointF(18, 18)).toPoint());
                bool monotonic = true, bounded = true, stableTextures = true, noSteps = true;
                int interpolatedFrames = 0;
                QRectF previous = before;
                QElapsedTimer reflowClock;
                reflowClock.start();
                qint64 previousTime = 0;
                while (reflowClock.elapsed() < 320) {
                    pump(8);
                    if (!cover) qFatal("FAIL visible album identity survives queue reflow");
                    const QRectF current = cover->mapRectToScene(cover->boundingRect());
                    const qint64 now = reflowClock.elapsed();
                    const qreal oldValues[] {before.x(), before.y(), before.width()};
                    const qreal endValues[] {destination.x(), destination.y(), destination.width()};
                    const qreal values[] {current.x(), current.y(), current.width()};
                    const qreal previousValues[] {previous.x(), previous.y(), previous.width()};
                    for (int axis = 0; axis < 3; ++axis) {
                        const qreal delta = endValues[axis] - oldValues[axis];
                        bounded &= values[axis] >= qMin(oldValues[axis], endValues[axis]) - .75
                            && values[axis] <= qMax(oldValues[axis], endValues[axis]) + .75;
                        monotonic &= (values[axis] - previousValues[axis]) * (delta < 0 ? -1 : 1) >= -.75;
                        if (now - previousTime < 40)
                            noSteps &= qAbs(values[axis] - previousValues[axis]) <= qAbs(delta) * .55 + 2;
                    }
                    if (qAbs(current.width()-before.width()) > .5 && qAbs(current.width()-destination.width()) > .5)
                        ++interpolatedFrames;
                    stableTextures &= cover->property("decodeSize") == originalDecode
                        && cover->property("renderTextureSize") == originalTexture;
                    previous = current; previousTime = now;
                }
                check(bounded && monotonic, "queue toggle moves album monotonically to final grid without size reversals");
                if (!noSteps || interpolatedFrames <= 5) qInfo() << "Grid reflow diagnostics" << viewportWidth << opening << noSteps << interpolatedFrames << before << destination << previous << grid->property("animateQueueLayout");
                check(noSteps && interpolatedFrames > 5, "album row wrapping and resizing interpolate across frames without jumps");
                check(stableTextures, "queue toggle does not reload artwork or reallocate cover texture sizes");
                const QRectF after = cover->mapRectToScene(cover->boundingRect());
                check(qAbs(after.x()-destination.x()) < 1 && qAbs(after.y()-destination.y()) < 1
                    && qAbs(after.width()-destination.width()) < 1, "album arrives at correct queue layout endpoint");
            }
        }
        grid->setProperty("contentY", grid->property("cellHeight").toReal() + 35);
        pump(100);
        QPointer<QQuickItem> scrolledCover = findGridItem(grid, "cardArtwork4");
        check(bool(scrolledCover), "scrolled album is instantiated before queue reflow");
        for (const bool opening : {true, false}) {
            const auto before = scrolledCover->mapToScene(QPointF(0, 0));
            shell.rootObject()->setProperty("queueOpen", opening);
            QGuiApplication::processEvents();
            check(bool(scrolledCover), "scrolled album survives queue layout change");
            const auto after = scrolledCover->mapToScene(QPointF(0, 0));
            check(qAbs(after.y()-before.y()) < 20, "queue reflow preserves scrolled album position at transition start");
            pump(350);
        }
        grid->setProperty("contentY", 0);
        pump(100);
        // Reversing an in-flight toggle keeps current visual geometry as the starting point.
        shell.rootObject()->setProperty("queueOpen", true);
        pump(75);
        auto* reversingCover = findGridItem(grid, "cardArtwork2");
        const auto reversingBounds = reversingCover->mapRectToScene(reversingCover->boundingRect());
        shell.rootObject()->setProperty("queueOpen", false);
        QGuiApplication::processEvents();
        const auto reversedBounds = reversingCover->mapRectToScene(reversingCover->boundingRect());
        check(qAbs(reversedBounds.x()-reversingBounds.x()) < 8 && qAbs(reversedBounds.y()-reversingBounds.y()) < 8,
            "reversing queue animation retains current album position");
        pump(350);
        client.setReduceMotion(true);
        shell.rootObject()->setProperty("queueOpen", true);
        pump(30);
        check(!grid->property("animateQueueLayout").toBool(), "reduced motion skips album reflow animation");
        shell.rootObject()->setProperty("queueOpen", false);
        client.setReduceMotion(false);
        shell.resize(beforeGridResize);
        client.cards()->replace(originalCards);
        pump(350);
        QObject::disconnect(reflowConnection);
        check(reflowFrames.size() > 20, "queue reflow presents continuous animation frames");
        std::sort(reflowFrames.begin(), reflowFrames.end());
        qInfo() << "Queue album reflow frame intervals median/p95/max ms" << reflowFrames[reflowFrames.size()/2]
            << reflowFrames[reflowFrames.size()*95/100] << reflowFrames.last();
        check(reflowFrames[reflowFrames.size()*95/100] < 25, "queue album reflow frame p95 stays under 25 ms");
        std::function<QQuickItem*(QQuickItem*)> findHoverCover = [&](QQuickItem* parent) -> QQuickItem* {
            if (parent->objectName() == "cardArtwork0")
                return parent;
            for (auto* child : parent->childItems())
                if (auto* found = findHoverCover(child))
                    return found;
            return nullptr;
        };
        auto* hoverCover = findHoverCover(grid);
        check(hoverCover, "first album cover is available for hover regression");
        shell.raise(); shell.requestActivate();
        check(waitFor([&] { return shell.isActive(); }), "artwork hover owner window is active");
        const auto unhoveredBounds = hoverCover->mapRectToScene(hoverCover->boundingRect());
        QTest::mouseMove(&shell, hoverCover->mapToScene(QPointF(80, 80)).toPoint());
        check(waitFor([&] { return hoverCover->property("imageScale").toReal() > 1.019; }),
            "actual pointer hover still zooms artwork");
        check(hoverCover->scale() == 1 && hoverCover->mapRectToScene(hoverCover->boundingRect()) == unhoveredBounds,
            "hover keeps rounded frame inside grid bounds");
        const auto hoverCenter = hoverCover->mapToScene(QPointF(hoverCover->width() / 2, hoverCover->height() / 2));
        const auto hoverFrame = shell.grabWindow();
        check(hoverFrame.pixelColor(qRound(hoverCenter.x() * shell.devicePixelRatio()),
                  qRound(hoverCenter.y() * shell.devicePixelRatio())).red() > 100,
            "hover artwork renders in normal window before item capture");
        auto hoveredGrab = hoverCover->grabToImage(QSize(240, 240));
        check(waitFor([&] { return !hoveredGrab->image().isNull(); }), "hovered cover captured");
        const auto hoveredImage = hoveredGrab->image();
        int hoveredEdgePixels = 0;
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 18; ++x) {
                const int alpha = hoveredImage.pixelColor(x, y).alpha();
                hoveredEdgePixels += alpha > 0 && alpha < 255;
            }
        check(hoveredImage.pixelColor(0, 0).alpha() == 0 && hoveredEdgePixels >= 4,
            "hovered cover preserves transparent antialiased corners");
        check(shell.grabWindow().save("/tmp/echo-quick-qa/albums-hover.png"), "hovered album screenshot saved");
        auto* playerCover = shell.rootObject()->findChild<QQuickItem*>("playerCover");
        check(playerCover, "bottom player cover remains available");
        auto playerCoverGrab = playerCover->grabToImage();
        check(waitFor([&] { return !playerCoverGrab->image().isNull(); }), "bottom player cover captured after hover");
        const auto playerCoverImage = playerCoverGrab->image();
        check(playerCoverImage.pixelColor(playerCoverImage.width() / 2, playerCoverImage.height() / 2).red() > 100,
            "hover composition preserves artwork in the bottom player");
        QTest::mouseMove(&shell, QPoint(100, 100));
        check(waitFor([&] { return hoverCover->property("imageScale").toReal() == 1; }),
            "artwork returns to original scale on pointer exit");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, grid->mapToScene(QPointF(80, 80)).toPoint());
        check(waitFor([&] {
            return client.page().value("kind") == "album" && !client.page().value("busy").toBool()
                && client.tracks()->count() > 0;
        }),
            "actual album card opens asynchronous detail");
        check(client.page().value("kind") == "album", "card opens correct detail type");
        client.back();
        check(waitFor([&] { return !client.page().value("busy").toBool() && client.cards()->count() == 2; }),
            "back restores album grid");
        pump(200);
        check(shell.grabWindow().save("/tmp/echo-quick-qa/albums.png"), "album grid screenshot saved");
        const auto playlist = library.createPlaylist(QStringLiteral("测试歌单"));
        library.addToPlaylist(playlist, { ids[0], otherIds[0], ids[0] });
        client.navigate("playlist", QString::number(playlist), QStringLiteral("测试歌单"));
        check(waitFor([&] { return !client.page().value("busy").toBool() && client.tracks()->count() == 3; }),
            "playlist duplicates retained");
        check(client.tracks()->get(0).value("key") != client.tracks()->get(2).value("key"),
            "duplicate tracks have distinct entry identities");
        client.selectRow(2);
        client.removeSelected();
        check(library.playlistTrackIds(playlist) == QVector<qint64> { ids[0], otherIds[0] },
            "removal targets selected duplicate position");
        library.undo();
        check(waitFor([&] { return !client.page().value("busy").toBool() && client.tracks()->count() == 3; }),
            "undo restores duplicate entry");
        client.moveTrack(0, 1);
        check(library.playlistTrackIds(playlist) == QVector<qint64> { otherIds[0], ids[0], ids[0] },
            "playlist reorder persists source positions");
        check(waitFor([&] { return !client.page().value("busy").toBool(); }), "reorder refresh completes");
        player.setReplaceContextQueue(false);
        player.playTracks({ ids[0], otherIds[1] }, 0, false);
        pump(200);
        client.playRow(0);
        check(waitFor([&] { return player.currentTrack().id == otherIds[0]; }), "single insertion setting is honored");
        check(player.queue().size() == 3, "insertion retains existing queue");
        player.setReplaceContextQueue(true);
        client.queueAction("move", 2, 1);
        client.queueAction("remove", 2);
        check(client.queue()->count() == 2, "queue changes reach core and model");
        auto* queueToggle = shell.rootObject()->findChild<QQuickItem*>("queueToggle");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, queueToggle->mapToScene(QPointF(18, 18)).toPoint());
        check(shell.rootObject()->property("queueOpen").toBool(), "queue panel opens through actual button");
        pump(300);
        auto* queueList = shell.rootObject()->findChild<QQuickItem*>("queueList");
        check(queueList, "queue view exists");
        auto* libraryContent = shell.rootObject()->findChild<QQuickItem*>("libraryContent");
        auto* openQueuePanel = shell.rootObject()->findChild<QQuickItem*>("queuePanel");
        check(libraryContent && qAbs(libraryContent->x() + libraryContent->width() - openQueuePanel->x()) < .5,
            "open queue meets library content without a blank vertical strip");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            queueList->mapToScene(QPointF(100, queueList->height() - 15)).toPoint());
        check(shell.rootObject()->property("queueOpen").toBool(), "blank space inside queue keeps it open");
        auto* dismissTrackList = shell.rootObject()->findChild<QQuickItem*>("trackList");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            dismissTrackList->mapToScene(QPointF(150, 24)).toPoint());
        check(shell.rootObject()->property("queueOpen").toBool(), "song selection does not dismiss queue");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            dismissTrackList->mapToScene(QPointF(150, dismissTrackList->height() - 20)).toPoint());
        check(!shell.rootObject()->property("queueOpen").toBool(), "blank track viewport dismisses queue");
        shell.rootObject()->setProperty("queueOpen", true);
        pump(300);
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            libraryContent->mapToScene(QPointF(libraryContent->width() / 2, 48)).toPoint());
        check(!shell.rootObject()->property("queueOpen").toBool(), "blank header background dismisses queue");
        shell.rootObject()->setProperty("queueOpen", true);
        pump(300);
        shell.raise(); shell.requestActivate();
        pump(100);
        const auto oldQueueFirst = player.queue().constFirst().token;
        const auto dragStart = queueList->mapToScene(QPointF(120, 92)).toPoint();
        const auto dragEnd = queueList->mapToScene(QPointF(120, 29)).toPoint();
        QTest::mousePress(&shell, Qt::LeftButton, Qt::NoModifier, dragStart);
        for (int i = 1; i <= 8; ++i) {
            QTest::mouseMove(&shell, dragStart + (dragEnd - dragStart) * i / 8, 20);
            pump(20);
        }
        QTest::mouseRelease(&shell, Qt::LeftButton, Qt::NoModifier, dragEnd);
        check(waitFor([&] { return player.queue().constFirst().token != oldQueueFirst; }),
            "actual queue drag changes core order");
        shell.rootObject()->setProperty("queueOpen", false);
        auto* queuePanel = shell.rootObject()->findChild<QQuickItem*>("queuePanel");
        check(waitFor([&] { return !queuePanel->isVisible(); }), "queue finishes explicit close animation");
        const auto beforeResize = shell.size();
        bool closedQueueStayedHidden = true, layoutFollowedWindow = true;
        for (int i = 0; i < 20; ++i) {
            shell.resize(1040 + (i % 10) * 70, 700 + (i % 5) * 15);
            pump(12);
            closedQueueStayedHidden &= !queuePanel->isVisible();
            layoutFollowedWindow &= qAbs(queuePanel->x() - shell.rootObject()->width()) < .5;
        }
        check(closedQueueStayedHidden, "closed queue never flashes during animated window resize");
        check(layoutFollowedWindow, "closed queue follows window edge every resize frame without lag");
        shell.rootObject()->setProperty("queueOpen", true);
        check(waitFor([&] { return queuePanel->property("openness").toReal() == 1; }), "queue opens after resize");
        bool openQueueFollowedWindow = true;
        for (int i = 0; i < 12; ++i) {
            shell.resize(1040 + i * 50, 740);
            pump(12);
            openQueueFollowedWindow &= qAbs(queuePanel->x() + queuePanel->width() - shell.rootObject()->width()) < .5;
        }
        check(openQueueFollowedWindow, "open queue stays attached to right edge throughout live resize");
        shell.rootObject()->setProperty("queueOpen", false);
        shell.resize(beforeResize);
        check(waitFor([&] { return !queuePanel->isVisible(); }), "queue closes cleanly after resize stress");
        auto* bar = shell.rootObject()->findChild<QQuickItem*>("playerBar");
        auto* transport = shell.rootObject()->findChild<QQuickItem*>("barTransport");
        auto* detail = shell.rootObject()->findChild<QQuickItem*>("nowPlaying");
        check(bar && bar->x() == 0 && bar->width() == shell.width(), "player bar spans full window width");
        check(qAbs(transport->mapToScene(QPointF(transport->width() / 2, 0)).x() - shell.width() / 2.) < 1,
            "transport is centered in window independently of track text");
        const QPoint blank = bar->mapToScene(QPointF(10, 70)).toPoint();
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, blank);
        check(shell.rootObject()->property("expanded").toBool(), "blank player bar opens detail");
        pump(80);
        check(detail->y() > 0 && detail->y() < detail->height() && detail->scale() == 1,
            "detail slides upward without scaling");
        const auto barPosition = bar->mapToScene(QPointF(0, 0));
        check(bar->isVisible() && bar->z() > detail->parentItem()->z() && bar->isEnabled(),
            "shared bottom bar stays above the sliding detail and remains interactive");
        QTest::keyClick(&shell, Qt::Key_Escape);
        check(!shell.rootObject()->property("expanded").toBool(), "Escape reverses an active slide");
        auto checkCollapseHandoff = [&] {
            check(waitFor([&] { return detail->isVisible() && detail->y() > 30; }),
                "detail slides back behind the shared bar during collapse");
            check(bar->isVisible() && bar->isEnabled(), "bottom bar remains interactive throughout collapse");
            const auto handoff = shell.grabWindow();
            const auto sample = bar->mapToScene(QPointF(10, 20));
            check(handoff.pixelColor(qRound(sample.x() * shell.devicePixelRatio()),
                      qRound(sample.y() * shell.devicePixelRatio())) == QColor("#24202d"),
                "exposed bottom area renders player bar instead of empty background");
            check(handoff.save("/tmp/echo-quick-qa/collapse-handoff.png"), "collapse handoff frame saved");
        };
        checkCollapseHandoff();
        pump(450);
        check(!detail->isVisible() && bar->isVisible() && bar->mapToScene(QPointF(0, 0)) == barPosition,
            "detail closes downward and restores bottom bar");
        auto* barButton = shell.rootObject()->findChild<QQuickItem*>("barToggle");
        const bool wasPlaying = playback.playing();
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, barButton->mapToScene(QPointF(28, 28)).toPoint());
        check(waitFor([&] { return playback.playing() != wasPlaying; })
                && !shell.rootObject()->property("expanded").toBool(),
            "transport button does not trigger background expansion");
        playback.toggle();
        check(!shell.rootObject()->findChild<QQuickItem*>("expandDetail"), "redundant expansion arrow removed");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, blank);
        check(shell.rootObject()->property("expanded").toBool(), "player bar expands actual detail");
        pump(500);
        check(bar->isVisible() && bar->isEnabled() && detail->y() == 0 && detail->height() == shell.height()
                && detail->property("topInset").toReal() == 48 && bar->mapToScene(QPointF(0, 0)) == barPosition,
            "expanded detail retains the same shared bar at the same position");
        auto* detailButton = barButton;
        check(!detail->findChild<QQuickItem*>("detailToggle"), "detail has no duplicate playback controls");
        const bool detailWasPlaying = playback.playing();
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, detailButton->mapToScene(QPointF(28, 28)).toPoint());
        check(waitFor([&] { return playback.playing() != detailWasPlaying; }),
            "shared playback controls operate inside detail");
        playback.toggle();
        check(shell.grabWindow().save("/tmp/echo-quick-qa/detail.png"), "migrated detail screenshot saved");
        QTest::keyClick(&shell, Qt::Key_Escape);
        checkCollapseHandoff();
        check(waitFor([&] { return !detail->isVisible() && bar->isEnabled(); }),
            "full collapse ends with interactive bottom bar");
        shell.rootObject()->setProperty("expanded", true);
        pump(450);
        shell.resize(1040, 680);
        pump(200);
        check(bar->width() == shell.width(), "player bar remains full width at minimum size");
        for (const auto* name :
            { "barShuffle", "barPrevious", "barNext", "barRepeat", "barVolume", "queueToggle" }) {
            auto* button = shell.rootObject()->findChild<QQuickItem*>(name);
            check(
                button && button->width() >= 44 && button->height() >= 44, "player controls retain 44 px hit targets");
            check(qAbs(button->mapToScene(QPointF(0, button->height() / 2)).y()
                      - bar->mapToScene(QPointF(0, bar->height() / 2)).y())
                    < 1,
                "player controls share the same vertical center");
        }
        check(shell.grabWindow().save("/tmp/echo-quick-qa/detail-small.png"), "minimum window screenshot saved");
        shell.resize(1180, 800);
        shell.rootObject()->setProperty("expanded", false);
        pump(500);
        client.navigate("playlist", QString::number(library.favoritesPlaylistId()), QStringLiteral("我喜欢的"));
        check(waitFor([&] { return !client.page().value("busy").toBool(); }), "favorites page loads");
        const int favoriteCount = client.tracks()->count();
        if (favoriteCount > 0) {
            client.favoriteRow(0);
            check(waitFor([&] {
                return !client.page().value("busy").toBool() && client.tracks()->count() == favoriteCount - 1;
            }),
                "unfavorite removes row from active favorites page");
            library.undo();
            check(waitFor([&] {
                return !client.page().value("busy").toBool() && client.tracks()->count() == favoriteCount;
            }),
                "undo restores active favorites page");
        }
        const auto favorites = library.favoritesPlaylistId();
        library.deletePlaylist(favorites);
        check(library.favoritesPlaylistId() == favorites, "favorites protection remains in core");
        client.setReduceMotion(true);
        shell.rootObject()->setProperty("expanded", true);
        pump(20);
        check(shell.rootObject()->property("reveal").toReal() == 1, "reduced motion skips transition");
        client.setReduceMotion(false);
        shell.rootObject()->setProperty("expanded", false);
        pump(500);
        // Every auxiliary surface uses the same Quick engine and asynchronous client requests.
        auto object = [&](const char* name) { return shell.rootObject()->findChild<QObject*>(name); };
        auto* settings = object("settingsDialog");
        check(settings, "QML settings surface exists");
        client.showSettings();
        check(waitFor([&] { return settings->property("opened").toBool(); }), "settings opens in Quick overlay");
        verifyFading(shell, client);
        check(shell.grabWindow().save("/tmp/echo-quick-qa/settings.png"), "QML settings screenshot saved");
        QTest::keyClick(&shell, Qt::Key_Escape);
        check(waitFor([&] { return !settings->property("visible").toBool(); }), "Escape dismisses settings");
        const bool oldMotion = client.reduceMotion();
        client.setPreference("reduceMotion", !oldMotion);
        check(client.preferences().value("reduceMotion").toBool() != oldMotion, "QML preference applies to backend");
        client.setPreference("reduceMotion", oldMotion);
        const auto playlistCount = library.playlists().size();
        client.action("newPlaylist");
        auto* nameDialog = object("nameDialog");
        auto* nameField = object("nameField");
        check(waitFor([&] { return nameDialog->property("opened").toBool(); }), "new playlist opens QML name dialog");
        QTest::keyClick(&shell, Qt::Key_Escape);
        check(waitFor([&] { return client.dialog().isEmpty(); }) && library.playlists().size() == playlistCount,
            "canceling name dialog creates no playlist");
        client.action("newPlaylist");
        client.action("newFolder");
        const int firstDialogToken = client.dialog().value("token").toInt();
        check(waitFor([&] { return nameDialog->property("opened").toBool(); }), "name dialog reopens");
        nameField->setProperty("text", "Quick auxiliary QA");
        QTest::keyClick(&shell, Qt::Key_Return);
        check(waitFor([&] { return client.dialog().value("title") == QStringLiteral("新建歌单文件夹"); }),
            "Enter creates playlist and advances queued dialog without losing request");
        check(library.playlists().size() == playlistCount + 1, "playlist creation reaches library");
        client.answerDialog(firstDialogToken, {{"accepted", false}});
        check(!client.dialog().isEmpty(), "stale dialog response cannot dismiss next request");
        QTest::keyClick(&shell, Qt::Key_Escape);
        check(waitFor([&] { return client.dialog().isEmpty(); }), "queued dialog cancels cleanly");
        for (const auto& p : library.playlists()) if (p.name == "Quick auxiliary QA") library.deletePlaylist(p.id);
        client.action("addSource");
        auto* folderPicker = object("folderPicker");
        check(waitFor([&] { return folderPicker->property("visible").toBool(); }), "folder selection opens dialog");
        check(folderPicker->property("options").toInt() == 0, "folder picker permits the native system dialog");
        check(QMetaObject::invokeMethod(folderPicker, "reject"), "folder picker exposes cancel");
        check(waitFor([&] { return client.dialog().isEmpty(); }), "folder picker cancel clears pending request");
        client.action("import");
        auto* filePicker = object("filePicker");
        check(waitFor([&] { return filePicker->property("visible").toBool(); }), "playlist import opens file dialog");
        check(filePicker->property("options").toInt() == 0, "file picker permits the native system dialog");
        QMetaObject::invokeMethod(filePicker, "reject");
        check(waitFor([&] { return client.dialog().isEmpty(); }), "file picker cancel clears pending request");
        client.navigate("songs", {}, QStringLiteral("歌曲"));
        check(waitFor([&] { return !client.page().value("busy").toBool() && client.tracks()->count() == 3; }), "songs restore after auxiliary dialog checks");
        check(waitFor([&] { return pageContent->isEnabled() && pageContent->opacity() == 1; }),
            "context menu owner finishes navigation transition");
        shell.raise(); shell.requestActivate();
        check(waitFor([&] { return shell.isActive(); }), "context menu owner window is active");
        QSignalSpy menus(&client, &echo::Client::menuRequested);
        auto* contextTracks = shell.rootObject()->findChild<QQuickItem*>("trackList");
        const auto contextPoint = contextTracks->mapToScene(QPointF(120, 24)).toPoint();
        QTest::mouseClick(&shell, Qt::RightButton, Qt::NoModifier, contextPoint);
        auto* contextPopup = object("appMenu");
        check(waitFor([&] { return contextPopup->property("opened").toBool(); }), "actual right click opens Quick menu");
        check(qAbs(contextPopup->property("x").toReal() - contextPoint.x()) < 2
            && qAbs(contextPopup->property("y").toReal() - contextPoint.y()) < 2,
            "context menu anchors at item click coordinates rather than stale global cursor");
        check(!menus.isEmpty(), "track context menu supplies Quick action data");
        int infoToken = 0;
        for (const auto& value : menus.last().at(0).toList()) {
            const auto entry = value.toMap();
            if (entry.value("text") == QStringLiteral("歌曲信息…")) infoToken = entry.value("token").toInt();
        }
        check(infoToken > 0, "track menu retains metadata action");
        check(QMetaObject::invokeMethod(object("appMenu"), "chosen", Q_ARG(int, infoToken)), "Quick menu activation routes selected action");
        auto* trackDialog = object("trackDialog");
        check(waitFor([&] { return trackDialog->property("opened").toBool(); }), "metadata editor opens in Quick");
        check(!client.dialog().value("trackTitle").toString().isEmpty(), "metadata editor receives selected track");
        check(shell.grabWindow().save("/tmp/echo-quick-qa/track-dialog.png"), "QML track editor screenshot saved");
        QTest::keyClick(&shell, Qt::Key_Escape);
        check(waitFor([&] { return client.dialog().isEmpty(); }), "metadata cancel leaves source untouched");
        client.action("menu"); // Also constructs nested Quick submenus.
        pump(100);
        QTest::keyClick(&shell, Qt::Key_Escape);
        pump(150);
        QWindow* mini = nullptr;
        for (auto* candidate : QGuiApplication::allWindows()) if (candidate->objectName() == "miniPlayer") mini = candidate;
        check(mini, "mini player is a QML Window");
        client.showMini();
        check(waitFor([&] { return mini->isVisible() && !shell.isVisible(); }), "mini player replaces main window");
        auto* miniSeek = mini->findChild<QQuickItem*>("miniSeek");
        check(miniSeek && miniSeek->isEnabled(), "mini player has working seek control");
        mini->requestActivate();
        check(waitFor([&] { return mini->isActive(); }), "mini player receives keyboard and pointer focus");
        const auto miniSeekPoint = miniSeek->mapToScene(QPointF(miniSeek->width() * .65, miniSeek->height()/2)).toPoint();
        QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, miniSeekPoint);
        check(waitFor([&] { return playback.position() > 11000 && playback.position() < 17000; }),
            "actual mini seek click reaches PCM playback position");
        check(qobject_cast<QQuickWindow*>(mini)->grabWindow().save("/tmp/echo-quick-qa/mini.png"), "QML mini player screenshot saved");
        const bool miniWasPlaying = playback.playing();
        mini->close();
        check(waitFor([&] { return !mini->isVisible(); }) && playback.playing() == miniWasPlaying,
            "closing mini hides its window without stopping playback");
        client.showMainWindow();
        check(waitFor([&] { return shell.isVisible() && !mini->isVisible(); }), "return from mini restores main window");
        shell.raise(); shell.requestActivate();
        pump(150);
        check(waitFor([&] { return app.applicationState() == Qt::ApplicationActive && !library.scanBusy(); }),
            "activation rescan completes before isolated offline stress fixture");
        // Large offline library fixture: isolates view/model load from media decoding.
        {
            echo::Database writer;
            check(writer.open(library.databasePath(), "quick-fixtures", &error), "stress writer opens");
            QSqlQuery transaction(QSqlDatabase::database("quick-fixtures"));
            check(transaction.exec("BEGIN"), "stress fixture transaction opens");
            auto t = library.track(ids[0]);
            t.rootId = sourceId;
            t.missing = true;
            for (int i = 0; i < 10000; ++i) {
                t.id = 0;
                t.path = temp.filePath(QStringLiteral("offline-%1.flac").arg(i));
                t.title = QStringLiteral("Archive %1").arg(i, 5, 10, QChar('0'));
                t.album = QStringLiteral("Archive %1").arg(i / 50);
                t.albumKey = echo::makeAlbumKey(t.albumArtist, t.artist, t.album);
                writer.upsertTrack(t);
            }
            check(transaction.exec("COMMIT"), "stress fixture transaction commits");
        }
        QSqlDatabase::removeDatabase("quick-fixtures");
        const auto previousToken = engine.activeToken();
        player.playTracks(ids, 0, false);
        check(waitFor([&] {
            return playback.playing() && player.queue().size() == 1 && engine.activeToken() != previousToken
                && engine.activeToken() == player.queue().constFirst().token && playback.position() > 200;
        }),
            "stress playback starts on the new audible token");
        pump(300);
        probe->underruns = 0;
        QElapsedTimer responseClock;
        responseClock.start();
        int ticks = 0;
        qint64 lastTick = 0, maxTickGap = 0;
        QTimer responsiveness;
        responsiveness.setInterval(5);
        QObject::connect(&responsiveness, &QTimer::timeout, &app, [&] {
            ++ticks;
            const auto now = responseClock.elapsed();
            maxTickGap = std::max(maxTickGap, now - lastTick);
            lastTick = now;
        });
        responsiveness.start();
        client.navigate("songs", {}, QStringLiteral("歌曲"));
        client.refresh(); // The synthetic fixture writes directly, without Library::changed.
        check(
            waitFor([&] { return !client.page().value("busy").toBool() && client.tracks()->count() == 10003; }, 15000),
            "ten-thousand-track library loads");
        responsiveness.stop();
        qInfo() << "10003 tracks load ms / UI ticks / max tick gap ms" << responseClock.elapsed() << ticks
                << maxTickGap;
        check(ticks >= 3 && maxTickGap < 50, "UI continues processing during large query");
        pump(100);
        list->setProperty("contentY", 0);
        QElapsedTimer wheelClock;
        wheelClock.start();
        const QPointF wheelPoint = list->mapToScene(QPointF(200, 120));
        auto sendListWheel = [&](QPoint pixels, QPoint angle, Qt::ScrollPhase phase, Qt::MouseEventSource source) {
            QWheelEvent event(wheelPoint, shell.mapToGlobal(wheelPoint.toPoint()), pixels, angle,
                Qt::NoButton, Qt::NoModifier, phase, false, source);
            event.setTimestamp(1 + wheelClock.elapsed());
            QCoreApplication::sendEvent(&shell, &event);
            pump(15);
        };
        bool consistentWheel = true;
        for (int i = 0; i < 6; ++i) {
            const qreal previousY = list->property("contentY").toReal();
            sendListWheel({}, QPoint(0, -120), Qt::NoScrollPhase, Qt::MouseEventNotSynthesized);
            consistentWheel &= qAbs(list->property("contentY").toReal() - previousY - 144) < 1;
        }
        check(consistentWheel, "each physical wheel notch scrolls three complete song rows");
        sendListWheel({}, QPoint(0, 120), Qt::NoScrollPhase, Qt::MouseEventNotSynthesized);
        check(qAbs(list->property("contentY").toReal() - 720) < 1, "wheel reverses immediately without residual inertia");
        sendListWheel(QPoint(0, -12), {}, Qt::NoScrollPhase, Qt::MouseEventNotSynthesized);
        check(qAbs(list->property("contentY").toReal() - 732) < 1,
            "pixel-only mouse wheels retain their precise distance");
        list->setProperty("contentY", 0);
        sendListWheel({}, {}, Qt::ScrollBegin, Qt::MouseEventSynthesizedBySystem);
        for (int i = 0; i < 6; ++i)
            sendListWheel(QPoint(0, -12), QPoint(0, -96), Qt::ScrollUpdate, Qt::MouseEventSynthesizedBySystem);
        sendListWheel({}, {}, Qt::ScrollEnd, Qt::MouseEventSynthesizedBySystem);
        const qreal preciseScroll = list->property("contentY").toReal();
        check(preciseScroll > 20 && preciseScroll < 100, "touchpad retains precise pixel scrolling without wheel amplification");
        client.setScroll(1000);
        list->setProperty("contentY", 1000);
        pump(100);
        check(
            std::abs(list->property("contentY").toReal() - 1000) < 2, "stress list actually scrolled before favorite");
        client.selectRow(30);
        client.favoriteRow(31);
        pump(40);
        check(
            waitFor([&] { return !client.page().value("busy").toBool(); }), "favorite update refreshes large library");
        pump(100);
        check(client.isSelected(30), "favorite refresh preserves selection identity");
        qInfo() << "Scroll actual/saved/origin" << list->property("contentY") << client.scroll()
                << list->property("originY");
        check(std::abs(list->property("contentY").toReal() - 1000) < 2, "favorite refresh preserves scroll");
        const auto content = qvariant_cast<QQuickItem*>(list->property("contentItem"));
        check(content && content->childItems().size() < 100,
            "ten-thousand-track list instantiates only visible delegates");
        QVector<double> shellFrames;
        QElapsedTimer shellFrameClock;
        shellFrameClock.start();
        qint64 shellPrevious = 0;
        const auto shellFrameConnection = QObject::connect(&shell, &QQuickWindow::frameSwapped, &app, [&] {
            const auto now = shellFrameClock.nsecsElapsed();
            if (shellPrevious)
                shellFrames << (now - shellPrevious) / 1e6;
            shellPrevious = now;
        });
        for (int i = 0; i < 20; ++i) {
            shell.rootObject()->setProperty("expanded", i % 2 == 0);
            pump(70);
        }
        QObject::disconnect(shellFrameConnection);
        check(shellFrames.size() > 40, "migrated shell presents continuous transition frames");
        std::sort(shellFrames.begin(), shellFrames.end());
        qInfo() << "Full shell frame intervals median/p95/max ms" << shellFrames[shellFrames.size() / 2]
                << shellFrames[shellFrames.size() * 95 / 100] << shellFrames.last();
        check(shellFrames[shellFrames.size() * 95 / 100] < 25, "full migrated shell 60 Hz transition p95 under 25 ms");
        pump(500);
        qInfo() << "Stress PCM underruns" << probe->underruns.load();
        check(probe->underruns == 0, "no PCM underruns during large library query and transitions");
        QSignalSpy closed(&shell, &echo::QuickView::hiddenToTray);
        shell.close();
        pump(30);
        check(!shell.isVisible() && closed.count() == 1 && playback.playing(),
            "close hides Quick window while playback continues");
        client.showMainWindow();
        check(shell.isVisible(), "resident action reopens Quick window");
        check(qmlIssues.isEmpty(), "no QML runtime warnings");
        if (app.arguments().contains("--preview")) {
            client.navigate("albums", {}, QStringLiteral("专辑"));
            QTimer::singleShot(300000, &app, [] { QCoreApplication::exit(0); });
            app.exec();
        }
        engine.stop();
        shell.hide();
        qInfo() << "PASS" << checks << "Quick integration checks; nonzero PCM samples" << probe->nonzero.load();
    }
}
