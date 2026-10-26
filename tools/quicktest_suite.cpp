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

int echo::quicktest::runInteractiveSuite(Harness& harness)
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
        window.rootObject()->setProperty("expanded", true);
        check(waitFor([&] {
            return window.rootObject()->property("reveal").toDouble() >= 1 && lyrics->isVisible() && lyrics->height() > 40;
        }, 2000), "lyrics surface is on screen");
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
            check(detail->findChild<QObject*>("playbackBackgroundImage")->property("source").toUrl().isEmpty()
                    && staticImage.pixelColor(backgroundPoint.toPoint()) != QColor("#16141d")
                    && library.setting("dynamic_playback_background") == "0",
                "disabled playback background uses the procedural default atmosphere");
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
                const qreal available = viewportWidth - 218 - 60;
                const qreal cell = qMin(226., available / 2);
                const int columns = qMax(2, int(available / cell));
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
                    if (QLineF(current.topLeft(), before.topLeft()).length() > .5 && QLineF(current.topLeft(), destination.topLeft()).length() > .5)
                        ++interpolatedFrames;
                    stableTextures &= cover->property("decodeSize") == originalDecode
                        && cover->property("renderTextureSize") == originalTexture;
                    previous = current; previousTime = now;
                }
                check(bounded && monotonic, "queue toggle moves album monotonically to final grid without size reversals");
                if (!noSteps || interpolatedFrames != 0) qInfo() << "Grid reflow diagnostics" << viewportWidth << opening << noSteps << interpolatedFrames << before << destination << previous << grid->property("animateQueueLayout");
                check(noSteps && interpolatedFrames == 0 && previous == before, "floating queue never reflows or resizes album artwork");
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
        check(reflowFrames.isEmpty(), "floating queue does not activate retained squeeze animation");
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
        check(libraryContent && qAbs(libraryContent->x() + libraryContent->width() - shell.width() + 30) < .5
                && openQueuePanel->x() < libraryContent->x() + libraryContent->width(),
            "open queue overlaps full-width library content");
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            queueList->mapToScene(QPointF(100, queueList->height() - 15)).toPoint());
        check(shell.rootObject()->property("queueOpen").toBool(), "blank space inside queue keeps it open");
        auto* dismissTrackList = shell.rootObject()->findChild<QQuickItem*>("trackList");
        const auto selectedBeforeDismiss = client.selectedCount();
        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
            dismissTrackList->mapToScene(QPointF(150, 24)).toPoint());
        check(!shell.rootObject()->property("queueOpen").toBool() && client.selectedCount() == selectedBeforeDismiss,
            "outside song click dismisses queue without changing selection");
        pump(300);
        shell.rootObject()->setProperty("queueOpen", true);
        pump(300);
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
            openQueueFollowedWindow &= qAbs(queuePanel->x() + queuePanel->width() - shell.rootObject()->width() + 12) < .5;
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
        verifyFading(shell, client, player, library);
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
        check(waitFor([&] {
            mini = nullptr;
            for (auto* candidate : QGuiApplication::allWindows())
                if (candidate->objectName() == "miniPlayer" && candidate->isVisible())
                    mini = candidate;
            return mini && !shell.isVisible();
        }), "mini player replaces main window");
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
        qInfo() << "PASS" << passedChecks() << "Quick integration checks; nonzero PCM samples" << probe->nonzero.load();

    return 0;
}
