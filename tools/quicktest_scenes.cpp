#include "quicktest_harness.h"
#include "presentation/theme.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEnterEvent>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QMutex>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QSignalSpy>
#include <QScreen>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

namespace {
QStringList& qmlIssues = echo::quicktest::issues();
using echo::quicktest::pump;
using echo::test::SilentOutput;
using echo::test::check;
using echo::test::passedChecks;
using echo::test::waitFor;
}

void echo::quicktest::verifyFading(QuickView& shell, Client& client, Player& player, Library& library)
{
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
}

int echo::quicktest::runOptionalScenes(Harness& harness)
{
    auto& app = harness.app;
    auto& temp = harness.temp;
    auto& library = harness.library;
    auto& error = harness.error;
    auto& ids = harness.ids;
    auto& tags = harness.tags;
    auto& otherIds = harness.otherIds;
    auto sourceId = harness.sourceId;
    auto* probe = harness.probe;
    auto& engine = harness.engine;
    auto& player = harness.player;
    auto& playback = harness.playback;
    auto& client = harness.client;
    auto& window = harness.window;
    const auto& coverPath = harness.coverPath;

        if (app.arguments().contains("--settings-window-test")) {
            client.attach(&window);
            client.setReduceMotion(true);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "settings test window exposed");
            auto* settings = window.rootObject()->findChild<QObject*>("settingsDialog");
            auto* navigation = window.rootObject()->findChild<QQuickItem*>("settingsNavigation");
            check(settings && navigation, "settings surfaces exist");
            const auto openSettings = [&](bool sidebar) {
                pump(400);
                const auto geometry = window.geometry();
                const auto state = window.windowState();
                QSignalSpy widths(&window, &QWindow::widthChanged);
                QSignalSpy heights(&window, &QWindow::heightChanged);
                QSignalSpy states(&window, &QWindow::windowStateChanged);
                if (sidebar)
                    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                        navigation->mapToScene(QPointF(navigation->width() / 2, navigation->height() / 2)).toPoint());
                else
                    client.showSettings();
                check(waitFor([&] { return settings->property("opened").toBool(); }), "settings opens");
                pump(500);
                if (window.geometry() != geometry || window.windowState() != state)
                    qInfo() << "Settings window geometry/state:" << geometry << state
                            << "->" << window.geometry() << window.windowState();
                check(window.geometry() == geometry && window.windowState() == state,
                    "opening settings preserves window geometry and state");
                check(widths.isEmpty() && heights.isEmpty() && states.isEmpty(),
                    "opening settings causes no intermediate window resize or state transition");
                check(QMetaObject::invokeMethod(settings, "close"), "settings closes");
                check(waitFor([&] { return !settings->property("visible").toBool(); }), "settings dismissed");
            };
            window.resize(1260, 820);
            openSettings(true);
            window.showMaximized();
            check(waitFor([&] { return window.maximized(); }), "settings test maximizes window");
            openSettings(true);
            openSettings(false);
            window.showFullScreen();
            check(waitFor([&] { return window.fullscreen(); }), "settings test enters fullscreen");
            pump(1000);
            openSettings(true);
            openSettings(false);
            window.showNormal();
            check(waitFor([&] { return window.visibility() == QWindow::Windowed; }), "settings test leaves fullscreen");
            pump(1000);
            window.hideToBackground();
            check(waitFor([&] { return !window.isVisible(); }), "settings test hides main window");
            client.showSettings();
            check(waitFor([&] { return window.isVisible() && settings->property("opened").toBool(); }),
                "settings restores hidden main window");
            QMetaObject::invokeMethod(settings, "close");
            pump(300);
            window.showMinimized();
            check(waitFor([&] { return window.visibility() == QWindow::Minimized; }), "settings test minimizes window");
            client.showSettings();
            check(waitFor([&] { return window.visibility() == QWindow::Windowed && settings->property("opened").toBool(); }),
                "settings restores minimized main window");
            check(qmlIssues.isEmpty(), "settings window test has no QML warnings");
            qInfo() << "PASS" << passedChecks() << "settings window checks";
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
            qInfo() << "PASS" << passedChecks() << "artist layout and navigation checks";
            return 0;
        }
        if (app.arguments().contains("--fading-test")) {
            echo::Client client(&library, &player, &engine);
            echo::QuickView shell(&playback, &client);
            client.attach(&shell);
            shell.show();
            check(QTest::qWaitForWindowExposed(&shell), "fading settings window exposed");
            client.showSettings();
            auto* settings = shell.rootObject()->findChild<QObject*>("settingsDialog");
            check(waitFor([&] { return settings->property("opened").toBool(); }), "fading settings opens");
            verifyFading(shell, client, player, library);
            QDir().mkpath("/tmp/echo-quick-qa");
            check(shell.grabWindow().save("/tmp/echo-quick-qa/settings-fading.png"), "fading settings screenshot saved");
            check(qmlIssues.isEmpty(), "fading settings QML has no warnings");
            qInfo() << "PASS" << passedChecks() << "fading settings and persistence checks";
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
                qInfo() << "PASS" << passedChecks() << "native fullscreen close checks";
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
            QQmlProperty::write(lyrics, "model", lines);
            QQmlProperty::write(lyrics, "activeLyricIndex", 0);
            QDir().mkpath("/tmp/echo-detail-layout");
            for (const QSize size : {QSize(1920, 1050), QSize(1440, 900), QSize(1180, 800), QSize(1040, 680)}) {
                QMetaObject::invokeMethod(shell.rootObject(), "showControls");
                shell.resize(size);
                const bool layoutReady = waitFor([&] {
                    return std::abs(content->width() - (cover->width() + content->property("spacing").toReal() + lyrics->width())) < 2
                        && std::abs(content->height() - (shell.height() - content->y() - 32 - detail->property("bottomInset").toReal())) < 1
                        && std::abs(cover->width() - std::min({520., (content->property("availableWidth").toReal() - content->property("spacing").toReal()) / 2., content->height() * .88})) < 1;
                });
                if (!layoutReady) qInfo() << "Unsettled layout" << shell.size()
                    << detail->size() << content->size() << cover->size() << lyrics->size() << content->property("spacing") << client.reduceMotion();
                check(layoutReady, "playback layout reaches the requested size");
                pump(100);
                const auto body = content->mapRectToScene(content->boundingRect());
                const auto art = cover->mapRectToScene(cover->boundingRect());
                const auto lyricArea = lyrics->mapRectToScene(lyrics->boundingRect());
                const auto controls = button->mapRectToScene(button->boundingRect());
                check(std::abs(body.center().x() - shell.width() / 2.) < 1,
                    "artwork and lyric reading block are centered together");
                check(std::abs((art.left() + lyricArea.right()) / 2. - shell.width() / 2.) < 1.5,
                    "visible cover and lyric bounds have balanced outer margins");
                const auto expectedColumnSize = std::min({520., (content->property("availableWidth").toReal() - content->property("spacing").toReal()) / 2., body.height() * .88});
                check(std::abs(art.width() - expectedColumnSize) < 1,
                    "equal columns fit the available width and height");
                check(std::abs(art.width() - lyricArea.width()) < 1,
                    "cover and lyric columns have equal widths");
                check(std::abs((art.right() + lyricArea.left()) / 2. - shell.width() / 2.) < 1,
                    "gap between cover and lyrics is symmetric about the window center");
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
                    const auto geometryMatchesWindow = [&] {
                        const qreal width = content->property("availableWidth").toReal();
                        const qreal height = std::max(0., shell.height() - content->y() - 32 - detail->property("bottomInset").toReal());
                        return std::abs(content->width() - (cover->width() + content->property("spacing").toReal() + lyrics->width())) < 2 && std::abs(content->height() - height) < 1
                            && std::abs(content->x() - ((shell.width() - content->width()) / 2)) < 1
                            && std::abs(cover->width() - std::min({520., (width - content->property("spacing").toReal()) / 2., height * .88})) < 1;
                    };
                    const auto lyricOffset = [&] {
                        auto* line = qvariant_cast<QQuickItem*>(lyrics->property("currentLine"));
                        return line ? line->mapToItem(lyrics, QPointF()).y()
                            - lyrics->height() * lyrics->property("readingAnchor").toReal()
                            : std::numeric_limits<qreal>::infinity();
                    };
                    const auto lyricAnchored = [&] { return std::abs(lyricOffset()) < 2; };
                    const auto verifyNoChase = [&] {
                        // Wait for an actual render/layout turn; a fixed delay
                        // alone can sample before a frame on a busy machine.
                        const bool anchored = waitFor(lyricAnchored, 250);
                        if (!anchored) qInfo() << "Unanchored lyric" << shell.size() << lyricOffset()
                            << lyrics->property("contentY") << lyrics->property("followedIndex") << lyrics->property("visibleIndex")
                            << lyrics->property("manualScrolling") << lyrics->property("count");
                        check(anchored, "lyric anchor follows the completed row layout");
                        pump(40);
                        check(geometryMatchesWindow(), "playback geometry follows the current window within one render turn");
                        if (!lyricAnchored()) qInfo() << "Resize lyric anchor" << shell.size() << lyricOffset()
                            << "scroll" << lyrics->property("contentY");
                        check(lyricAnchored(), "resized lyrics immediately retain the current reading anchor");
                        const auto art = coverRect();
                        const auto viewport = lyrics->mapRectToScene(lyrics->boundingRect());
                        const qreal scroll = lyrics->property("contentY").toReal();
                        for (int i = 0; i < 10; ++i) {
                            pump(40);
                            check(coverRect() == art && lyrics->mapRectToScene(lyrics->boundingRect()) == viewport
                                    && std::abs(lyrics->property("contentY").toReal() - scroll) < 1,
                                "cover and lyrics never chase the window after resizing stops");
                        }
                    };
                    shell.resize(1180, 800);
                    pump(100);
                    client.setReduceMotion(false);
                    QQmlProperty::write(lyrics, "activeLyricIndex", 6);
                    pump(500);
                    const auto textureSize = cover->property("renderTextureSize").toSize();
                    shell.resize(1440, 900);
                    verifyNoChase();
                    shell.resize(1180, 800);
                    verifyNoChase();
                    // Feed a live-resize stream, including reversals before any
                    // old 320 ms layout animation could have caught up.
                    for (int width : {1200, 1280, 1380, 1440, 1390, 1320, 1240, 1180}) {
                        shell.resize(width, 800 + (width - 1180) / 3);
                        pump(40);
                        check(waitFor(lyricAnchored, 250), "live lyric layout preserves its reading anchor");
                        check(geometryMatchesWindow(), "live resize and reversals keep playback aligned with the window");
                        check(lyricAnchored(), "live resize keeps the current lyric anchored");
                    }
                    verifyNoChase();
                    check(cover->property("renderTextureSize").toSize() == textureSize,
                        "cover mask texture stays allocated at the same size throughout resizing");
                    client.setReduceMotion(true);
                    shell.resize(1440, 900);
                    verifyNoChase();
                    shell.resize(1180, 800);
                    pump(40);
                    client.setReduceMotion(false);
                    // A normal lyric change still scrolls; resizing must not
                    // globally disable the existing 420 ms follow animation.
                    const qreal beforeFollow = lyrics->property("contentY").toReal();
                    QQmlProperty::write(lyrics, "activeLyricIndex", 7);
                    pump(80);
                    check(std::abs(lyrics->property("contentY").toReal() - beforeFollow) > 1 && !lyricAnchored(),
                        "normal playback lyric changes retain intermediate scrolling positions");
                    pump(450);
                    check(lyricAnchored(), "normal lyric scrolling finishes at the reading anchor");
                    // Wrap long lines at both widths before checking the anchor.
                    auto wrappedLines = lines;
                    for (auto& value : wrappedLines) {
                        auto line = value.toMap();
                        line["text"] = line["text"].toString().repeated(5);
                        value = line;
                    }
                    QQmlProperty::write(lyrics, "model", wrappedLines);
                    pump(500);
                    shell.resize(1040, 680);
                    verifyNoChase();
                    QQuickItem* wrappedLine = nullptr;
                    QMetaObject::invokeMethod(lyrics, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, wrappedLine), Q_ARG(int, 7));
                    if (!wrappedLine || wrappedLine->height() <= 64) qInfo() << "Wrapped lyric" << wrappedLine
                        << (wrappedLine ? wrappedLine->property("text") : QVariant()) << lyrics->property("currentIndex")
                        << lyrics->property("activeLyricIndex") << (wrappedLine ? wrappedLine->size() : QSizeF());
                    check(wrappedLine && wrappedLine->height() > 64, "long lyric fixture actually wraps across multiple rows");
                    shell.resize(1440, 900);
                    verifyNoChase();
                    lyrics->setProperty("manualScrolling", true);
                    lyrics->setProperty("contentY", lyrics->property("contentY").toReal() + 100);
                    const qreal manualY = lyrics->property("contentY").toReal();
                    shell.resize(1440, 800);
                    pump(40);
                    check(lyrics->property("manualScrolling").toBool()
                            && std::abs(lyrics->property("contentY").toReal() - manualY) < 1,
                        "viewport resize preserves the user's manual lyric position");
                    pump(450);
                    check(std::abs(lyrics->property("contentY").toReal() - manualY) < 1,
                        "manual lyrics are not pulled back after resizing");
                    const qreal resumeOffset = lyrics->property("displayOffset").toReal();
                    lyrics->setProperty("manualScrolling", false);
                    const qreal immediateResumeOffset = lyrics->property("displayOffset").toReal();
                    check(std::abs(immediateResumeOffset - resumeOffset) < 1,
                        "resuming automatic following starts at the actual manually displayed lyric position");
                    pump(80);
                    check(std::abs(lyrics->property("displayOffset").toReal() - resumeOffset) > 1 && !lyricAnchored(),
                        "manual-to-automatic handoff retains intermediate scrolling frames");
                    pump(450);
                    check(lyricAnchored(), "manual-to-automatic handoff finishes at the reading anchor");
                    QQmlProperty::write(lyrics, "model", QVariantList{});
                    shell.resize(1440, 900);
                    pump(40);
                    check(geometryMatchesWindow() && lyrics->property("count").toInt() == 0, "empty lyrics resize without invalid positioning");
                    QQmlProperty::write(lyrics, "model", lines);
                    lyrics->setProperty("manualScrolling", false);
                    shell.resize(1180, 800);
                    pump(500);
                    client.setReduceMotion(true);
                    for (int index : {0, int(lines.size() - 1)}) {
                        QQmlProperty::write(lyrics, "activeLyricIndex", index);
                        verifyNoChase();
                    }
                    QQmlProperty::write(lyrics, "activeLyricIndex", 7);
                    client.setReduceMotion(false);
                    const QSize restoredSize = shell.size();
                    // The standard fixture resembles the default cover. Use a
                    // distinct large image so reloads and fallback flashes cannot hide.
                    QImage zoomArtwork(2400, 2400, QImage::Format_RGB32);
                    zoomArtwork.fill(QColor("#27c58a"));
                    const auto zoomArtworkPath = temp.filePath("zoom-artwork.png");
                    check(zoomArtwork.save(zoomArtworkPath), "distinct resize artwork saved");
                    QQmlProperty::write(cover, "artwork", QUrl::fromLocalFile(zoomArtworkPath).toString());
                    auto* coverImage = cover->findChild<QQuickItem*>("coverImage");
                    auto* coverEffect = cover->findChild<QQuickItem*>("coverEffect");
                    check(coverImage && coverEffect, "resize artwork rendering surfaces exist");
                    check(waitFor([&] { return coverImage->property("status").toInt() == 1; }), "resize artwork is ready before zoom");
                    for (int i = 0; i < 4; ++i) {
                        if (i == 2) {
                            // Exercise the exact-row layout on a long transcript,
                            // with wrapping, rather than only a short song.
                            QVariantList transcript;
                            for (int row = 0; row < 1000; ++row)
                                transcript << QVariantMap{{"text", QStringLiteral("长篇歌词与逐行换行验证 %1").arg(row).repeated(row % 2 ? 1 : 4)}, {"time", row * 1000}};
                            client.setReduceMotion(true);
                            QQmlProperty::write(lyrics, "model", transcript);
                            QQmlProperty::write(lyrics, "activeLyricIndex", 500);
                            pump(100);
                            check(lyrics->property("count").toInt() == 1000 && lyricAnchored(),
                                "a thousand variable-height lyric rows have an exact reading anchor");
                            client.setReduceMotion(false);
                        }
                        struct ResizeFrame {
                            int windowWidth;
                            QRectF cover;
                            QRectF lyrics;
                            bool followsWindow;
                            qreal lyricOffset;
                            bool artworkVisible;
                            int decodeSize;
                            qint64 time;
                            qreal rowHeight;
                            qreal glyphSize;
                            qreal rowGap;
                            qreal typography;
                            qreal scrollY;
                            qreal scrollItemY;
                        };
                        struct FrameCapture {
                            QMutex mutex;
                            QVector<ResizeFrame> frames;
                            ResizeFrame synchronizedFrame{};
                            bool synchronized = false;
                        };
                        const auto capture = std::make_shared<FrameCapture>();
                        const int initialWidth = shell.width();
                        qInfo() << "Lyric pixel alignment" << lyrics->property("pixelAligned");
                        auto* metricRow = qvariant_cast<QQuickItem*>(lyrics->property("currentLine"));
                        QQuickItem* nextMetricRow = nullptr;
                        for (auto* row : metricRow->parentItem()->childItems())
                            if (row->property("index").toInt() == metricRow->property("index").toInt() + 1)
                                nextMetricRow = row;
                        check(nextMetricRow && !metricRow->childItems().isEmpty(), "neighbor lyric geometry is available for native resize diagnostics");
                        auto* glyph = metricRow->childItems().first();
                        QElapsedTimer renderClock;
                        renderClock.start();
                        // Cocoa's synchronous zoom pumps resize/expose events,
                        // but not QTimers. Read geometry while the GUI thread is
                        // blocked for scene-graph sync, then record completed frames.
                        const auto sync = QObject::connect(&shell, &QQuickWindow::beforeSynchronizing, &shell, [&, capture] {
                            capture->synchronizedFrame = {shell.width(), coverRect(), lyrics->mapRectToScene(lyrics->boundingRect()),
                                geometryMatchesWindow(), lyricOffset(), coverImage->property("status").toInt() == 1
                                    && qvariant_cast<QQuickItem*>(coverEffect->property("source")) == coverImage,
                                cover->property("decodeSize").toInt(), renderClock.nsecsElapsed(), metricRow->height(),
                                glyph->property("font").value<QFont>().pixelSize() * glyph->scale(),
                                nextMetricRow->y() - metricRow->y(), detail->property("typographyProgress").toReal(), lyrics->property("contentY").toReal(),
                                qvariant_cast<QQuickItem*>(lyrics->property("contentItem"))->y()};
                            capture->synchronized = true;
                        }, Qt::DirectConnection);
                        const auto rendered = QObject::connect(&shell, &QQuickWindow::afterFrameEnd, &shell, [capture] {
                            if (!capture->synchronized) return;
                            const QMutexLocker lock(&capture->mutex);
                            capture->frames << capture->synchronizedFrame;
                        }, Qt::DirectConnection);
#ifdef Q_OS_MACOS
                        // QTest mouse events bypass Cocoa's native event monitor.
                        postNativeTitlebarDoubleClick(&shell);
#else
                        shell.toggleMaximized();
#endif
                        check(waitFor([&] { return (shell.size() != restoredSize) == (i % 2 == 0); }),
                            "native titlebar double click enlarges and restores the window");
                        verifyNoChase();
                        QObject::disconnect(sync);
                        QObject::disconnect(rendered);
                        const QMutexLocker lock(&capture->mutex);
                        const auto& frames = capture->frames;
                        QFile metrics(QString("/tmp/echo-detail-layout/lyric-metrics-%1.csv").arg(i));
                        check(metrics.open(QIODevice::WriteOnly), "native lyric metric trace opens");
                        QTextStream trace(&metrics);
                        trace << "width,time_ms,row_height,glyph_size,row_gap,typography,anchor_error,scroll_y,scroll_item_y\n";
                        for (const auto& frame : frames)
                            trace << frame.windowWidth << ',' << frame.time / 1e6 << ',' << frame.rowHeight << ',' << frame.glyphSize
                                  << ',' << frame.rowGap << ',' << frame.typography << ',' << frame.lyricOffset << ',' << frame.scrollY << ',' << frame.scrollItemY << '\n';
                        check(!frames.isEmpty(), "native zoom presents sampled render frames");
                        const int minimumWidth = std::min(initialWidth, shell.width());
                        const int maximumWidth = std::max(initialWidth, shell.width());
                        int followingFrames = 0, coverChanges = 0, lyricChanges = 0;
                        QRectF previousCover, previousLyrics;
                        bool allFollowing = true;
                        bool allArtwork = true, stableDecode = true;
                        qreal maximumLyricOffset = 0, maximumGlyphError = 0, maximumGapError = 0;
                        int fractionalGlyphFrames = 0;
                        const int initialDecode = frames.first().decodeSize;
                        QVector<double> intervals;
                        qint64 previousTime = 0;
                        for (const auto& frame : frames) {
                            allArtwork &= frame.artworkVisible;
                            stableDecode &= frame.decodeSize == initialDecode;
                            if (frame.windowWidth <= minimumWidth || frame.windowWidth >= maximumWidth)
                                continue;
                            if (previousTime) intervals << (frame.time - previousTime) / 1000000.;
                            previousTime = frame.time;
                            maximumLyricOffset = std::max(maximumLyricOffset, std::abs(frame.lyricOffset));
                            maximumGlyphError = std::max(maximumGlyphError, std::abs(frame.glyphSize - (20 + 2 * frame.typography)));
                            fractionalGlyphFrames += std::abs(frame.glyphSize - std::round(frame.glyphSize)) > .01;
                            if (i < 2)
                                maximumGapError = std::max(maximumGapError, std::abs(frame.rowGap - (32 * frame.glyphSize / 22 + 18 + 2 * frame.typography)));
                            allFollowing &= frame.followsWindow;
                            followingFrames += frame.followsWindow;
                            coverChanges += frame.cover != previousCover;
                            lyricChanges += frame.lyrics != previousLyrics;
                            previousCover = frame.cover;
                            previousLyrics = frame.lyrics;
                        }
                        qInfo() << "Native zoom rendered frames" << i << "total" << frames.size()
                                << "following during zoom" << followingFrames << "cover changes" << coverChanges
                                << "lyric viewport changes" << lyricChanges << "max lyric anchor error" << maximumLyricOffset
                                << "artwork visible" << allArtwork << "stable decode" << stableDecode;
                        if (!intervals.isEmpty()) {
                            std::sort(intervals.begin(), intervals.end());
                            qInfo() << "Native zoom frame intervals ms median/p95/max" << intervals[intervals.size() / 2]
                                    << intervals[intervals.size() * 95 / 100] << intervals.last();
                        }
                        check(allArtwork && stableDecode, "native zoom keeps the real artwork without reloads or fallback frames");
                        check(maximumLyricOffset < .01, "automatic lyric rendering retains a subpixel reading anchor through native resize");
                        check(maximumGlyphError < .01 && maximumGapError < .01,
                            "glyph scale and adjacent lyric spacing change continuously without integral font-size steps");
                        if (maximumWidth > 1400 && minimumWidth <= 1180)
                            check(fractionalGlyphFrames >= 2, "native resize renders multiple fractional text scales rather than three discrete font sizes");
#ifdef Q_OS_MACOS
                        check(allFollowing && followingFrames >= 4 && coverChanges >= 4 && lyricChanges >= 4,
                            "cover and lyrics follow intermediate native zoom sizes in completed render frames");
#else
                        // Some window managers maximize instantly, without resize frames.
                        check(allFollowing, "any intermediate native zoom frames follow the window");
#endif
                    }
                    const auto artworkFrame = shell.grabWindow();
                    const auto artworkCenter = coverRect().center() * shell.devicePixelRatio();
                    const auto artworkPixel = artworkFrame.pixelColor(qRound(artworkCenter.x()), qRound(artworkCenter.y()));
                    check(std::abs(artworkPixel.red() - 39) < 3 && std::abs(artworkPixel.green() - 197) < 3
                            && std::abs(artworkPixel.blue() - 138) < 3,
                        "rendered artwork pixels retain the distinct real image after native zoom");
                    check(artworkFrame.save("/tmp/echo-detail-layout/resize-content-verified.png"), "native zoom artwork proof saved");
                    for (bool entering : {true, false}) {
                        if (entering) shell.showFullScreen(); else shell.showNormal();
                        check(waitFor([&] {
                            return shell.fullscreen() == entering
                                && shell.property("echoFullscreenTransition").toString().isEmpty()
                                && (QGuiApplication::platformName() != "cocoa"
                                    || shell.property("echoNativeFullscreen").toBool() == entering);
                        }), "native fullscreen transition finishes");
                        verifyNoChase();
                        check(shell.grabWindow().save(entering ? "/tmp/echo-detail-layout/fullscreen-entered.png"
                                                             : "/tmp/echo-detail-layout/fullscreen-exited.png"),
                            "native fullscreen layout screenshot saved");
                    }
                    check(shell.size() == restoredSize, "fullscreen exit restores the original window size");
                    check(qmlIssues.isEmpty(), "playback resize has no QML warnings");
                    qInfo() << "PASS" << passedChecks() << "playback resize checks";
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
            QQmlProperty::write(lyrics, "model", fadeLines);
            QQmlProperty::write(lyrics, "activeLyricIndex", -1);
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
            QQmlProperty::write(lyrics, "model", lines);
            lyrics->setProperty("manualScrolling", false);
            QQmlProperty::write(lyrics, "activeLyricIndex", 6);
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
            QQmlProperty::write(lyrics, "model", QVariantList{});
            pump(100);
            check(shell.grabWindow().save("/tmp/echo-detail-layout/detail-empty.png"), "empty lyric layout saved");
            QQmlProperty::write(lyrics, "model", lines);
            QQmlProperty::write(lyrics, "activeLyricIndex", -1);
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
            QQmlProperty::write(lyrics, "model", lines);
            QQmlProperty::write(lyrics, "activeLyricIndex", 6);
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
            auto* floatingQueue = shell.rootObject()->findChild<QQuickItem*>("queuePanel");
            auto* floatingList = shell.rootObject()->findChild<QQuickItem*>("queueList");
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
                    && lyrics->mapRectToScene(lyrics->boundingRect()) == lyricsBeforeQueue && floatingQueue->isVisible(),
                "playback cover and lyrics retain their layout under the shared queue");
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
            qInfo() << "PASS" << passedChecks() << "detail layout checks";
            return 0;
        }

    return -1;
}
