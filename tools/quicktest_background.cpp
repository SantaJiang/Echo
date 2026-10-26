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

int echo::quicktest::runBackgroundTest(Harness& harness)
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

            // AppKit fullscreen rendering needs the native application loop.
            QTimer::singleShot(0, &app, [&] {
                echo::Client client(&library, &player, &engine);
                echo::QuickView shell(&playback, &client);
                client.attach(&shell);
                client.setReduceMotion(true);
                player.playTracks(ids, 0, false);
                check(waitFor([&] { return playback.playing(); }), "background fixture starts");
                player.toggle();
                check(waitFor([&] { return !playback.playing(); }), "background fixture pauses");
                shell.rootObject()->setProperty("expanded", true);
                shell.show();
                check(QTest::qWaitForWindowExposed(&shell), "background window exposed");
                auto* background = shell.rootObject()->findChild<QQuickItem*>("playbackBackground");
                check(background, "dedicated background exists");
                auto* outputItem = background->findChild<QQuickItem*>("playbackBackgroundOutput");
                auto* image = background->findChild<QQuickItem*>("playbackBackgroundImage");
                check(background && outputItem && image, "dedicated background surfaces exist");
                QDir().mkpath("/tmp/echo-background-qa");
                auto capture = [&](const QString& name) {
                    qInfo() << "Capturing background" << name << shell.size() << shell.devicePixelRatio();
                    shell.raise();
                    shell.requestActivate();
                    pump(150);
                    auto grab = outputItem->grabToImage();
                    // Force a frame even if Cocoa has temporarily occluded the
                    // window; asynchronous item grabs otherwise wait for exposure.
                    shell.grabWindow();
                    check(grab && waitFor([&] { return !grab->image().isNull(); }), "background captured at physical resolution");
                    const auto result = grab->image();
                    check(result.save("/tmp/echo-background-qa/" + name + ".png"), "background proof saved");
                    return result;
                };
                check(!client.preferences().value("dynamicPlaybackBackground").toBool(), "dynamic background defaults off");
                const auto defaultImage = capture("default");
                check(!background->property("usesArtwork").toBool() && image->property("source").toUrl().isEmpty(),
                    "default atmosphere does not load a low-resolution gradient");
                check(outputItem->property("status").toInt() == 0, "background shader compiled successfully");
                // In a smooth dark gradient, dithering must break up flat quantized
                // bands while keeping adjacent output pixels within two code values.
                int reversals = 0;
                int samples = 0;
                int largestStep = 0;
                for (int y = defaultImage.height() / 8; y < defaultImage.height() / 2; y += 13) {
                    int previousStep = 0;
                    for (int x = defaultImage.width() / 2; x < defaultImage.width() - 1; ++x) {
                        const auto left = defaultImage.pixelColor(x - 1, y);
                        const auto right = defaultImage.pixelColor(x, y);
                        const int step = right.red() - left.red();
                        reversals += step * previousStep < 0;
                        if (step) previousStep = step;
                        largestStep = std::max({largestStep, qAbs(step), qAbs(right.green() - left.green()),
                                               qAbs(right.blue() - left.blue())});
                        ++samples;
                    }
                }
                qInfo() << "Background dither reversals/samples/max step" << reversals << samples << largestStep;
                check(reversals > samples / 12 && largestStep <= 2, "subtle output dithering breaks continuous color bands");
                check(capture("default-still") == defaultImage, "dither pattern stays identical between frames");
                pump(350);
                QSignalSpy idleFrames(&shell, &QQuickWindow::frameSwapped);
                pump(350);
                check(idleFrames.count() <= 2, "paused background does not continuously render");

                client.showSettings();
                auto* settings = shell.rootObject()->findChild<QObject*>("settingsDialog");
                check(waitFor([&] { return settings->property("opened").toBool(); }), "background settings opens");
                auto* setting = shell.rootObject()->findChild<QQuickItem*>("dynamicBackgroundSetting");
                check(setting && !setting->property("checked").toBool(), "dynamic background setting is unchecked");
                QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                                 setting->mapToScene(QPointF(12, setting->height() / 2)).toPoint());
                check(client.preferences().value("dynamicPlaybackBackground").toBool()
                          && library.setting("dynamic_playback_background") == "1", "background preference persists");
                QMetaObject::invokeMethod(settings, "close");
                check(waitFor([&] { return !settings->property("visible").toBool(); }), "background settings closes");
                check(waitFor([&] { return background->property("usesArtwork").toBool(); }), "dynamic background loads current cover");
                check(capture("dynamic-cover") != defaultImage, "cover changes the actual rendered background");
                QImage darkCover(512, 256, QImage::Format_RGB32);
                darkCover.fill(QColor("#13101b"));
                check(darkCover.save(temp.filePath("dark.png")), "dark cover fixture saved");
                QImage contrastCover(512, 256, QImage::Format_RGB32);
                contrastCover.fill(Qt::black);
                {
                    QPainter painter(&contrastCover);
                    painter.fillRect(256, 0, 256, 256, QColor("#ffe0f0"));
                }
                check(contrastCover.save(temp.filePath("contrast.png")), "contrast cover fixture saved");
                for (const QString name : {QStringLiteral("dark"), QStringLiteral("contrast")}) {
                    background->setProperty("artwork", QUrl::fromLocalFile(temp.filePath(name + ".png")));
                    check(waitFor([&] { return image->property("status").toInt() == 1; }), "alternate cover loaded");
                    const auto frame = capture("dynamic-" + name);
                    check(frame != defaultImage && frame.pixelColor(frame.width() / 2, frame.height() / 2).alpha() == 255,
                          "alternate artwork renders an opaque background");
                    check(capture("dynamic-" + name + "-still") == frame, "dynamic background noise stays static");
                }
                pump(350);
                QSignalSpy dynamicIdleFrames(&shell, &QQuickWindow::frameSwapped);
                pump(350);
                check(dynamicIdleFrames.count() <= 2, "paused dynamic background does not continuously render");
                background->setProperty("artwork", QUrl());
                check(waitFor([&] { return !background->property("usesArtwork").toBool(); }), "missing artwork falls back immediately");
                check(capture("no-artwork") == defaultImage, "missing artwork uses the same default atmosphere");
                QFile brokenCover(temp.filePath("broken-cover.png"));
                check(brokenCover.open(QIODevice::WriteOnly), "broken artwork fixture created");
                brokenCover.close();
                background->setProperty("artwork", QUrl::fromLocalFile(brokenCover.fileName()));
                check(waitFor([&] { return image->property("status").toInt() == 3; }), "broken artwork reports a load error");
                check(capture("broken-artwork") == defaultImage, "artwork load failure falls back to default atmosphere");
                // The intentionally invalid fixture is the only expected QML warning.
                qmlIssues.removeIf([](const QString& issue) {
                    return issue.contains("Error decoding:") && issue.contains("broken-cover.png");
                });
                client.setPreference("dynamicPlaybackBackground", false);
                check(library.setting("dynamic_playback_background") == "0", "disabling background persists");
                background->setProperty("artwork", QUrl::fromLocalFile(temp.filePath("contrast.png")));
                check(capture("disabled") == defaultImage && image->property("source").toUrl().isEmpty(),
                      "disabled dynamic background ignores available artwork");
                for (const QSize size : {QSize(1040, 680), QSize(1440, 900)}) {
                    shell.resize(size);
                    pump(250);
                    capture(QString("default-%1x%2").arg(size.width()).arg(size.height()));
                    check(shell.grabWindow().save(QString("/tmp/echo-background-qa/page-%1x%2.png").arg(size.width()).arg(size.height())),
                          "playback page screenshot saved");
                }
                shell.showMaximized();
                check(waitFor([&] { return shell.visibility() == QWindow::Maximized; }), "background window maximizes");
                capture("maximized");
                shell.showFullScreen();
                check(waitFor([&] { return shell.fullscreen(); }), "background window enters fullscreen");
#ifdef Q_OS_MACOS
                check(waitFor([&] { return shell.property("echoNativeFullscreen").toBool()
                    && shell.property("echoFullscreenTransition").toString().isEmpty(); }), "native fullscreen settles");
#endif
                pump(500);
                capture("fullscreen");
                check(shell.grabWindow().save("/tmp/echo-background-qa/page-fullscreen.png"), "fullscreen page screenshot saved");
                shell.showNormal();
                check(waitFor([&] { return !shell.fullscreen(); }), "background returns to windowed mode");
#ifdef Q_OS_MACOS
                check(waitFor([&] { return !shell.property("echoNativeFullscreen").toBool()
                    && shell.property("echoFullscreenTransition").toString().isEmpty(); }), "native fullscreen exit settles");
#endif
                check(qmlIssues.isEmpty(), "background rendering produces no QML warnings");
                qInfo() << "PASS" << passedChecks() << "focused playback background checks; DPR" << shell.devicePixelRatio()
                        << "graphics API" << shell.rendererInterface()->graphicsApi();
                QCoreApplication::exit(0);
            });
            return app.exec();
}
