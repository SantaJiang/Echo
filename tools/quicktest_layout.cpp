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

int echo::quicktest::runLayoutContract(Harness& harness)
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

            QTimer::singleShot(0, &app, [&] {
                echo::Client client(&library, &player, &engine);
                echo::QuickView shell(&playback, &client);
                client.attach(&shell);
                client.setReduceMotion(true);
                shell.show();
                check(QTest::qWaitForWindowExposed(&shell), "layout contract window exposed");
                const auto find = [&](const char* name) {
                    auto* item = shell.rootObject()->findChild<QQuickItem*>(name);
                    check(item, "layout contract surface exists");
                    return item;
                };
                auto* sidebar = find("navigationSidebar");
                auto* browser = find("libraryContent");
                auto* queue = find("queuePanel");
                auto* bar = find("playerBar");
                auto* transport = find("barTransport");
                auto* trackInfo = find("barTrackInfo");
                auto* actions = find("barActions");
                const auto rect = [](QQuickItem* item) { return item->mapRectToScene(item->boundingRect()); };
                if (app.arguments().contains("--glass-sidebar-test")) {
                    auto* canvas = find("shellCanvas");
                    const auto tint = [&] { return sidebar->property("color").value<QColor>(); };
                    const bool available = shell.property("sidebarBackdropAvailable").toBool();
                    const bool active = shell.property("sidebarBackdrop").toBool();
                    check(active == (available && client.preferences().value("glassSidebar").toBool()),
                        "the sidebar hole follows the preference and the platform capability");
                    check(canvas->x() == (active ? 218. : 0.),
                        "the base plate only leaves the sidebar column to the backdrop while it is active");
                    check(active ? (tint().alpha() > 0 && tint().alpha() < 255) : tint().alpha() == 255,
                        "the sidebar is tinted only while a backdrop is active");
                    client.setPreference("glassSidebar", false);
                    pump(60);
                    check(canvas->x() == 0 && tint().alpha() == 255 && !shell.property("sidebarBackdrop").toBool(),
                        "disabling the preference restores the opaque sidebar at runtime");
                    client.setPreference("glassSidebar", true);
                    pump(60);
                    check(qmlIssues.isEmpty(), "the glass sidebar preference produces no QML warnings");
                    QCoreApplication::exit(0);
                    return;
                }
                if (app.arguments().contains("--floating-queue-test")) {
                    QDir().mkpath("/tmp/echo-floating-queue");
                    shell.resize(1180, 800);
                    shell.raise(); shell.requestActivate();
                    pump(250);
                    auto* list = find("queueList");
                    auto* toggle = find("queueToggle");
                    const auto clickToggle = [&] {
                        QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier,
                            toggle->mapToScene(QPointF(22, 22)).toPoint());
                    };
                    QVector<QVariantMap> rows;
                    for (int i = 0; i < 30; ++i)
                        rows << QVariantMap{{"title", QString("Queue track %1").arg(i)}, {"artist", "Echo"},
                            {"artwork", ""}, {"current", i == 0}};
                    client.queue()->replace(rows);
                    const auto bodyBefore = rect(browser);
                    const auto before = shell.grabWindow();
                    client.setReduceMotion(false);
                    clickToggle(); pump(65);
                    check(queue->property("openness").toReal() > 0 && queue->property("openness").toReal() < 1,
                        "shared floating queue animates on the library page");
                    check(waitFor([&] { return queue->property("openness").toReal() == 1; }), "shared queue finishes opening");
                    check(rect(browser) == bodyBefore, "library geometry is unchanged by floating queue");
                    const auto bounds = rect(queue);
                    check(bounds == QRectF(848, 60, 320, 800 - bar->height() - 72), "shared queue has consistent insets and width");
                    const auto frame = shell.grabWindow();
                    const auto sample = QPoint(qRound(230 * shell.devicePixelRatio()), qRound(400 * shell.devicePixelRatio()));
                    check(before.pixelColor(sample) == frame.pixelColor(sample), "floating queue does not dim the surrounding page");
                    auto* shadow = find("queueShadow");
                    check(shadow->property("shadowEnabled").toBool() && shadow->property("blurMax").toInt() == 24,
                        "shared queue uses a bounded background shadow");
                    check(frame.save("/tmp/echo-floating-queue/library.png"), "shared library queue screenshot saved");
                    shadow->setProperty("shadowEnabled", false); pump(60);
                    const auto noShadow = shell.grabWindow();
                    const QPoint shadowSample(qRound((bounds.left() - 4) * shell.devicePixelRatio()),
                        qRound(bounds.center().y() * shell.devicePixelRatio()));
                    check(frame.pixelColor(shadowSample).lightness() < noShadow.pixelColor(shadowSample).lightness(),
                        "rendered shadow darkens pixels immediately outside the panel");
                    shadow->setProperty("shadowEnabled", true); pump(60);
                    list->setProperty("contentY", 180);
                    pump(80);
                    const auto scroll = list->property("contentY").toReal();
                    shell.rootObject()->setProperty("expanded", true);
                    pump(450);
                    check(queue->isVisible() && queue->property("openness").toReal() == 1
                            && list->property("contentY").toReal() == scroll && rect(queue) == bounds,
                        "same floating queue and scroll position survive entering playback");
                    check(shell.grabWindow().save("/tmp/echo-floating-queue/playback.png"), "shared playback queue screenshot saved");
                    shell.rootObject()->setProperty("expanded", false);
                    pump(450);
                    check(list->property("contentY").toReal() == scroll && queue->isVisible(), "queue scroll survives leaving playback");
                    const auto pageBeforeDismiss = client.page();
                    auto* tracks = find("trackList");
                    const auto trackScroll = tracks->property("contentY");
                    const auto wheelPoint = tracks->mapToScene(QPointF(80, 100));
                    QWheelEvent outsideWheel(wheelPoint, shell.mapToGlobal(wheelPoint.toPoint()), QPoint(), QPoint(0, -120),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(&shell, &outsideWheel); pump(60);
                    check(tracks->property("contentY") == trackScroll && queue->isVisible(), "outside wheel neither scrolls the page nor dismisses queue");
                    QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, QPoint(80, 138));
                    check(!shell.rootObject()->property("queueOpen").toBool() && client.page() == pageBeforeDismiss,
                        "outside sidebar click closes queue without navigating");
                    check(waitFor([&] { return !queue->isVisible(); }), "outside dismissal finishes");
                    clickToggle(); pump(70); clickToggle(); pump(40); clickToggle();
                    check(waitFor([&] { return queue->property("openness").toReal() == 1; }), "rapid toggle reversal finishes open");
                    for (int i = 0; i < 15; ++i) {
                        shell.resize(1040 + i * 20, 700 + i * 5); pump(12);
                        check(std::abs(rect(queue).right() - shell.width() + 12) < .5, "floating queue tracks live window resizing");
                    }
                    QTest::keyClick(&shell, Qt::Key_Escape);
                    check(waitFor([&] { return !queue->isVisible(); }), "Escape dismisses library queue");
                    client.setReduceMotion(true); clickToggle();
                    check(queue->property("openness").toReal() == 1, "reduced motion opens shared queue immediately");
                    auto* close = find("queueCloseButton");
                    QTest::mouseClick(&shell, Qt::LeftButton, Qt::NoModifier, close->mapToScene(QPointF(15, 15)).toPoint());
                    check(!queue->isVisible(), "close button immediately dismisses reduced motion queue");
                    check(qmlIssues.isEmpty(), "floating queue has no QML warnings");
                    qInfo() << "PASS" << passedChecks() << "floating queue checks";
                    QCoreApplication::exit(0);
                    return;
                }
                if (app.arguments().contains("--window-stability-review")) {
                    QDir().mkpath("/tmp/echo-window-stability");
                    auto* dialogs = shell.rootObject()->findChild<QObject*>("appDialogs");
                    check(dialogs, "dialog controller exists for all-window stability review");
                    const auto bounds = [](QObject* popup) {
                        return QRectF(popup->property("x").toReal(), popup->property("y").toReal(),
                            popup->property("width").toReal(), popup->property("height").toReal());
                    };
                    const auto centeredError = [&](QObject* popup) {
                        const auto box = bounds(popup);
                        return std::max(std::abs(box.center().x() - shell.width()/2.), std::abs(box.center().y() - shell.height()/2.));
                    };
                    const auto nativeCycle = [&](const std::function<qreal()>& error, const QString& label) {
#ifdef Q_OS_MACOS
                        const auto restored = shell.size();
                        for (int direction = 0; direction < 2; ++direction) {
                            struct Capture { QMutex mutex; qreal pending = 0; qreal maximum = 0; int count = 0; };
                            auto capture = std::make_shared<Capture>();
                            auto sync = QObject::connect(&shell, &QQuickWindow::beforeSynchronizing, &shell, [&, capture] {
                                capture->pending = error();
                            }, Qt::DirectConnection);
                            auto rendered = QObject::connect(&shell, &QQuickWindow::afterFrameEnd, &shell, [capture] {
                                const QMutexLocker lock(&capture->mutex);
                                capture->maximum = std::max(capture->maximum, capture->pending);
                                ++capture->count;
                            }, Qt::DirectConnection);
                            postNativeTitlebarDoubleClick(&shell);
                            check(waitFor([&] { return (shell.size() != restored) == (direction == 0); }), "overlay owner handles native zoom and restore");
                            pump(80);
                            QObject::disconnect(sync); QObject::disconnect(rendered);
                            const QMutexLocker lock(&capture->mutex);
                            qInfo() << "Overlay native alignment" << label << direction << "frames" << capture->count << "max error" << capture->maximum;
                            check(capture->count >= 4 && capture->maximum <= 1.01, "overlay follows its owner within one logical pixel in every rendered native resize frame");
                        }
#else
                        Q_UNUSED(error); Q_UNUSED(label);
#endif
                    };
                    const QList<QSize> sizes{{1180,800},{1440,900},{1200,760},{1040,680},{1380,860},{1180,800}};
                    for (const bool reduced : {false, true}) {
                        client.setReduceMotion(reduced);
                        for (const QString name : {QString("nameDialog"),QString("messageDialog"),QString("trackDialog"),QString("settingsDialog")}) {
                            auto* dialog = shell.rootObject()->findChild<QObject*>(name);
                            check(dialog, "application dialog exists");
                            QQmlProperty::write(dialogs,"request", QVariantMap{{"kind",name == "messageDialog" ? "message" : "name"},
                                {"title",QStringLiteral("窗口位置验证")}, {"text",QStringLiteral("长内容自动换行并滚动。 ").repeated(300)},
                                {"path",QString("/long-path/").repeated(30)}});
                            check(QMetaObject::invokeMethod(dialog,"open"), "application dialog opens without changing user data");
                            check(waitFor([&] { return dialog->property("opened").toBool(); }), "application dialog opening settles");
                            for (const auto size : sizes) {
                                shell.resize(size); pump(40);
                                const auto box = bounds(dialog);
                                check(centeredError(dialog) <= 1.01, "dialog remains centered through resize and reversals");
                                check(box.left() >= 19 && box.top() >= 19 && box.right() <= shell.width()-19 && box.bottom() <= shell.height()-19,
                                    "dialog remains inside the owner viewport");
                                auto* footer = dialog->findChild<QQuickItem*>("dialogActions");
                                auto* body = dialog->findChild<QQuickItem*>("dialogBodyScroll");
                                check(footer && body && rect(footer).top() >= rect(body).bottom()-1 && rect(footer).bottom() <= box.bottom()+1,
                                    "dialog actions stay below scroll body during all resize directions");
                            }
                            if (!reduced) nativeCycle([&] { return centeredError(dialog); },name);
                            check(shell.grabWindow().save("/tmp/echo-window-stability/"+name+".png"), "dialog stability proof saved");
                            QMetaObject::invokeMethod(dialog,"close"); pump(60);
                        }
                    }
                    shell.resize(1180,800);
                    auto* volume = shell.rootObject()->findChild<QObject*>("barVolumePopup");
                    check(volume && QMetaObject::invokeMethod(volume,"open"), "volume popup opens"); pump(120);
                    const auto volumeError = [&] {
                        auto* background = qvariant_cast<QQuickItem*>(volume->property("background"));
                        const auto box = rect(background);
                        return std::max(std::abs(box.right()-(shell.width()-22)), std::abs(box.bottom()-(rect(bar).top()-12)));
                    };
                    for (const auto size : sizes) { shell.resize(size); pump(40); check(volumeError() <= 1.01, "volume popup follows the bottom-right player bar without drift"); }
                    nativeCycle(volumeError,"volumePopup");
                    QMetaObject::invokeMethod(volume,"close");
                    auto* menu = shell.rootObject()->findChild<QObject*>("appMenu");
                    check(menu, "context menu exists");
                    emit client.menuRequested(QVariantList{QVariantMap{{"text",QStringLiteral("位置检查")},{"token",-1}},
                        QVariantMap{{"text",QStringLiteral("子菜单")},{"children",QVariantList{QVariantMap{{"text","child"},{"token",-2}}}}}});
                    pump(100);
                    for (const auto size : sizes) {
                        shell.resize(size); pump(40);
                        if (!menu->property("visible").toBool()) continue;
                        const auto box = bounds(menu);
                        check(box.left() >= -1 && box.top() >= -1 && box.right() <= shell.width()+1 && box.bottom() <= shell.height()+1,
                            "open context menu remains within its resized owner");
                    }
                    QMetaObject::invokeMethod(menu,"close");
                    auto* owner = find("barVolume");
                    auto* hint = owner->findChild<QObject*>("echoToolTip");
                    check(hint, "edge tooltip exists");
                    QQmlProperty::write(hint,"visible",true); pump(150);
                    for (const auto size : sizes) {
                        shell.resize(size); pump(40);
                        if (!hint->property("visible").toBool()) continue;
                        auto* background = qvariant_cast<QQuickItem*>(hint->property("background"));
                        const auto box = rect(background);
                        check(box.left() >= -1 && box.top() >= -1 && box.right() <= shell.width()+1 && box.bottom() <= shell.height()+1,
                            "edge tooltip stays inside its resized owner");
                    }
                    QQmlProperty::write(hint,"visible",false);
                    shell.resize(1180,800);
                    QQuickWindow* tray = nullptr;
                    for (auto* window : shell.rootObject()->findChildren<QQuickWindow*>())
                        if (window->width() == 190 && window->height() == 132) tray=window;
                    check(tray, "tray popup window exists");
                    const auto screenBox = shell.screen()->geometry();
                    shell.raise(); shell.requestActivate();
                    check(waitFor([&] { return shell.isActive(); }), "tray fixture owner activation has settled");
                    for (int repeat=0; repeat<6; ++repeat) {
                        if (repeat % 2) shell.hideToBackground(); else client.showMainWindow();
                        pump(100);
                        for (const auto point : {screenBox.topLeft(),screenBox.topRight(),screenBox.bottomLeft(),screenBox.bottomRight()}) {
                            struct Capture { QMutex mutex; QRect pending; QVector<QRect> frames; };
                            auto capture=std::make_shared<Capture>();
                            auto sync=QObject::connect(tray,&QQuickWindow::beforeSynchronizing,tray,[&,capture] { capture->pending=tray->geometry(); },Qt::DirectConnection);
                            auto rendered=QObject::connect(tray,&QQuickWindow::afterFrameEnd,tray,[capture] {
                                const QMutexLocker lock(&capture->mutex); capture->frames << capture->pending;
                            },Qt::DirectConnection);
                            emit client.trayMenuRequested(point);
                            check(waitFor([&] { const QMutexLocker lock(&capture->mutex); return !capture->frames.isEmpty(); }), "tray popup presents a completed frame with visible or hidden main window");
                            pump(80);
                            QObject::disconnect(sync); QObject::disconnect(rendered);
                            const QMutexLocker lock(&capture->mutex);
                            bool bounded=true,stable=true;
                            for (const auto& frame : capture->frames) {
                                bounded &= screenBox.adjusted(-1,-1,1,1).contains(frame);
                                stable &= frame==capture->frames.first();
                            }
                            if (!bounded || !stable) qInfo() << "Tray rendered geometry" << repeat << point << screenBox << capture->frames;
                            check(bounded, "all rendered tray popup frames remain inside physical screen corners");
                            check(stable, "tray popup has no visible delayed positioning correction");
                            tray->hide(); pump(40);
                        }
                    }
                    shell.resize(1180,800); shell.show(); shell.raise(); shell.requestActivate();
                    client.showSettings();
                    auto* settings = shell.rootObject()->findChild<QObject*>("settingsDialog");
                    check(waitFor([&] { return settings->property("opened").toBool(); }), "settings opens before fullscreen recovery");
                    for (const bool entering : {true,false}) {
                        if (entering) shell.showFullScreen(); else shell.showNormal();
                        check(waitFor([&] { return shell.fullscreen()==entering && shell.property("echoFullscreenTransition").toString().isEmpty()
                            && (QGuiApplication::platformName()!="cocoa" || shell.property("echoNativeFullscreen").toBool()==entering); }), "fullscreen transition settles with modal overlay open");
                        check(centeredError(settings) <= 1.01, "settings remains centered across native fullscreen transitions");
                    }
                    QMetaObject::invokeMethod(settings,"close");
                    check(qmlIssues.isEmpty(), "window and overlay stability matrix has no QML warnings");
                    qInfo() << "PASS" << passedChecks() << "window and overlay stability checks";
                    QCoreApplication::exit(0);
                    return;
                }
                if (app.arguments().contains("--album-resize-test")) {
                    client.navigate("albums", {}, QStringLiteral("专辑"));
                    check(waitFor([&] { return !client.page().value("busy").toBool() && client.cards()->count() > 0; }), "album resize fixture loads");
                    client.setReduceMotion(false);
                    shell.resize(1180, 800);
                    shell.raise();
                    shell.requestActivate();
                    pump(600);
                    auto* grid = find("cardGrid");
                    const auto firstCard = client.cards()->get(0);
                    QVector<QVariantMap> resizeCards;
                    for (int i = 0; i < 30; ++i) {
                        auto card = firstCard;
                        card["key"] = QString("resize-%1").arg(i);
                        QImage tileImage(800, 800, QImage::Format_RGB32);
                        tileImage.fill(QColor::fromHsv((i * 37) % 360, 150, 180));
                        const auto imagePath = temp.filePath(QString("album-resize-%1.png").arg(i));
                        check(tileImage.save(imagePath), "distinct album artwork saved");
                        card["artwork"] = QUrl::fromLocalFile(imagePath).toString();
                        resizeCards << card;
                    }
                    client.cards()->replace(resizeCards);
                    pump(350);
                    std::function<QQuickItem*(QQuickItem*)> findArtwork = [&](QQuickItem* parent) -> QQuickItem* {
                        if (parent->objectName() == "cardArtwork5") return parent;
                        for (auto* child : parent->childItems()) if (auto* found = findArtwork(child)) return found;
                        return nullptr;
                    };
                    auto* artwork = findArtwork(grid);
                    check(artwork, "visible album artwork exists");
                    const int restoredWidth = shell.width();
                    for (int cycle = 0; cycle < 4; ++cycle) {
                        if (cycle == 2) { shell.rootObject()->setProperty("queueOpen", true); pump(350); }
                        auto* image = artwork->findChild<QQuickItem*>("coverImage");
                        check(image && waitFor([&] { return image->property("status").toInt() == 1; }), "album image ready before native zoom");
                        const int decode = artwork->property("decodeSize").toInt();
                        struct Frame { int width; qreal cell; QRectF cover; bool ready; int decode; qint64 time; };
                        struct Capture { QMutex mutex; QVector<Frame> frames; Frame pending{}; };
                        auto capture = std::make_shared<Capture>();
                        QElapsedTimer clock;
                        clock.start();
                        auto sync = QObject::connect(&shell, &QQuickWindow::beforeSynchronizing, &shell, [&, capture] {
                            capture->pending = {shell.width(), grid->property("cellWidth").toReal(), rect(artwork), image->property("status").toInt() == 1, artwork->property("decodeSize").toInt(), clock.nsecsElapsed()};
                        }, Qt::DirectConnection);
                        auto rendered = QObject::connect(&shell, &QQuickWindow::afterFrameEnd, &shell, [capture] {
                            const QMutexLocker lock(&capture->mutex);
                            capture->frames << capture->pending;
                        }, Qt::DirectConnection);
#ifdef Q_OS_MACOS
                        postNativeTitlebarDoubleClick(&shell);
#else
                        shell.toggleMaximized();
#endif
                        check(waitFor([&] { return (shell.width() != restoredWidth) == (cycle % 2 == 0); }), "album native zoom changes size");
                        pump(450);
                        QObject::disconnect(sync);
                        QObject::disconnect(rendered);
                        const QMutexLocker lock(&capture->mutex);
                        QVector<double> intervals;
                        qreal maxJump = 0, maxUnexpectedSizeStep = 0, maxPositionStep = 0;
                        bool stableArtwork = true;
                        int frames = 0;
                        Frame previous{};
                        for (const auto& frame : capture->frames) {
                            stableArtwork &= frame.ready && frame.decode == decode;
                            if (frame.width == restoredWidth || frame.width == shell.width()) continue;
                            if (previous.time) {
                                intervals << (frame.time - previous.time) / 1000000.;
                                maxJump = std::max(maxJump, std::abs(frame.cover.width() - previous.cover.width()));
                                maxUnexpectedSizeStep = std::max(maxUnexpectedSizeStep, std::abs(frame.cover.width() - previous.cover.width()) - std::abs(frame.width - previous.width) / 2.);
                                if (frame.time - previous.time < 40000000) maxPositionStep = std::max(maxPositionStep, QLineF(frame.cover.topLeft(), previous.cover.topLeft()).length());
                            }
                            previous = frame;
                            ++frames;
                        }
                        bool settledWithWindow = true;
                        for (const auto& frame : capture->frames)
                            if (frame.width == shell.width()) settledWithWindow &= QLineF(frame.cover.topLeft(), capture->frames.last().cover.topLeft()).length() < 1;
                        check(settledWithWindow, "album layout has no second movement after native zoom ends");
                        std::sort(intervals.begin(), intervals.end());
                        qInfo() << "Album native zoom" << cycle << "total frames" << capture->frames.size() << "width" << shell.width() << "frames" << frames << "cover max step" << maxJump << "position max step" << maxPositionStep << "first/last" << capture->frames.first().cover << capture->frames.last().cover
                                << "frame ms median/p95/max" << (intervals.isEmpty() ? 0 : intervals[intervals.size()/2])
                                << (intervals.isEmpty() ? 0 : intervals[intervals.size()*95/100]) << (intervals.isEmpty() ? 0 : intervals.last());
                        check(stableArtwork, "album resize retains ready artwork and constant decode size in every rendered frame");
#ifdef Q_OS_MACOS
                        check(frames >= 4 && !intervals.isEmpty(), "album native resize presents multiple intermediate frames");
                        check(maxUnexpectedSizeStep < 1, "column breakpoints never grow or shrink album covers abruptly");
                        check(intervals[intervals.size()*95/100] < 33.4, "album native resize p95 stays within two 60 Hz frames");

#endif
                    }
                    check(qmlIssues.isEmpty(), "album resize has no QML warnings");
                    qInfo() << "PASS" << passedChecks() << "album resize checks";
                    QCoreApplication::exit(0);
                    return;
                }
                QDir().mkpath("/tmp/echo-layout-refactor");
                for (const QString& kind : {QStringLiteral("songs"), QStringLiteral("albums"), QStringLiteral("album"),
                         QStringLiteral("artists"), QStringLiteral("artist"), QStringLiteral("sources"), QStringLiteral("source"),
                         QStringLiteral("playlist"), QStringLiteral("search")}) {
                    if (kind == "search") client.search("layout-no-matching-track");
                    else {
                        const QString key = kind == "playlist" ? QString::number(library.favoritesPlaylistId())
                            : kind == "source" ? QString::number(sourceId)
                            : kind == "album" ? library.albums().first().key
                            : kind == "artist" ? library.artists().first().key : QString();
                        client.navigate(kind, key, kind);
                    }
                    check(waitFor([&] { return !client.page().value("busy").toBool() && client.page().value("kind") == kind; }),
                        "library route loads through the shared declarative browser");
                    for (const QSize size : {QSize(1040, 680), QSize(1180, 800), QSize(1440, 900)}) {
                        shell.resize(size);
                        for (bool opened : {false, true}) {
                            shell.rootObject()->setProperty("queueOpen", opened);
                            pump(60);
                            const auto side = rect(sidebar), body = rect(browser), panel = rect(queue), controls = rect(bar);
                            check(std::abs(side.width() - 218) < 1 && std::abs(side.bottom() - controls.top()) < 1,
                                "sidebar keeps its layout column above the player bar");
                            check(std::abs(body.left() - side.right() - 30) < 1
                                    && std::abs(body.right() - shell.width() + 30) < 1,
                                "all library pages retain full workspace margins with floating queue");
                            check(body.width() > 200 && body.height() > 200 && body.bottom() <= controls.top() + 1,
                                "library content stays usable and inside the window at every tested size");
                            check(std::abs(rect(transport).center().x() - shell.width() / 2.) < 1,
                                "equal side zones keep player transport centered independently of metadata");
                            check(rect(trackInfo).right() <= rect(transport).left()
                                    && rect(actions).left() >= rect(transport).right(),
                                "player bar zones never overlap at narrow or wide window sizes");
                        }
                    }
                    check(shell.grabWindow().save("/tmp/echo-layout-refactor/" + kind + ".png"), "library layout proof saved");
                    if (app.arguments().contains("--ui-motion-review")) {
                        shell.rootObject()->setProperty("queueOpen", false);
                        client.setReduceMotion(false);
                        shell.resize(1180, 800);
                        pump(350);
                        for (int zoom = 0; zoom < 2; ++zoom) {
                            struct Capture { QMutex mutex; QVector<qint64> times; QElapsedTimer clock; };
                            auto capture = std::make_shared<Capture>();
                            capture->clock.start();
                            auto connection = QObject::connect(&shell, &QQuickWindow::afterFrameEnd, &shell, [capture] {
                                const QMutexLocker lock(&capture->mutex);
                                capture->times << capture->clock.nsecsElapsed();
                            }, Qt::DirectConnection);
#ifdef Q_OS_MACOS
                            postNativeTitlebarDoubleClick(&shell);
#else
                            shell.toggleMaximized();
#endif
                            check(waitFor([&] { return (shell.width() != 1180) == (zoom == 0); }), "each library page handles native zoom and restore");
                            pump(350);
                            QObject::disconnect(connection);
                            const QMutexLocker lock(&capture->mutex);
                            QVector<double> intervals;
                            for (int i = 1; i < capture->times.size(); ++i)
                                intervals << (capture->times[i] - capture->times[i-1]) / 1000000.;
                            std::sort(intervals.begin(), intervals.end());
                            qInfo() << "UI native motion" << kind << zoom << "frames" << capture->times.size()
                                    << "p95 ms" << (intervals.isEmpty() ? 0 : intervals[intervals.size()*95/100]);
#ifdef Q_OS_MACOS
                            check(intervals.size() >= 4 && intervals[intervals.size()*95/100] < 33.4,
                                "each library page renders native resizing within two 60 Hz frames at p95");
#endif
                        }
                        client.setReduceMotion(true);
                    }
                }
                check(find("trackList")->property("reuseItems").toBool() && find("cardGrid")->property("reuseItems").toBool()
                        && find("artistList")->property("reuseItems").toBool(), "large library views retain delegate virtualization");
                client.navigate("albums", {}, QStringLiteral("专辑"));
                check(waitFor([&] { return !client.page().value("busy").toBool() && client.cards()->count() > 0; }),
                    "album artwork loads before responsive resource checks");
                auto* grid = find("cardGrid");
                const auto decode = grid->property("artworkResolution");
                check(decode.toInt() >= 208 * shell.devicePixelRatio() * 2, "album decode preserves high-resolution artwork through the effect pipeline");
                const auto texture = grid->property("artworkTextureSide");
                for (int width : {1040, 1108, 1110, 1112, 1180, 1440}) {
                    shell.resize(width, 800);
                    pump(40);
                    check(grid->property("artworkResolution") == decode && grid->property("artworkTextureSide") == texture,
                        "album resources stay stable across window resizing and column breakpoints");
                }
                shell.rootObject()->setProperty("queueOpen", false);
                shell.resize(1040, 680);
                client.showSettings();
                auto* settings = shell.rootObject()->findChild<QObject*>("settingsDialog");
                check(settings && waitFor([&] { return settings->property("opened").toBool(); }), "settings opens in bounded dialog");
                pump(60);
                check(settings->property("y").toReal() >= 19
                        && settings->property("y").toReal() + settings->property("height").toReal() <= shell.height() - 19,
                    "settings dialog fits the minimum-height window");
                QMetaObject::invokeMethod(settings, "close");
                auto* dialogs = shell.rootObject()->findChild<QObject*>("appDialogs");
                check(dialogs, "dialog presentation controller is explicit");
                QQmlProperty::write(dialogs, "request", QVariantMap{{"kind", "message"}, {"title", QStringLiteral("长内容布局验证")},
                    {"text", QStringLiteral("验证长文本会在对话框内自然换行并滚动。 ").repeated(300)}});
                auto* message = shell.rootObject()->findChild<QObject*>("messageDialog");
                check(message && QMetaObject::invokeMethod(message, "open"), "long message dialog opens");
                pump(100);
                auto* scroll = message->findChild<QQuickItem*>("dialogBodyScroll");
                check(scroll && scroll->property("contentHeight").toReal() > scroll->height(),
                    "long dialog content overflows into a real scroll container");
                check(message->property("height").toReal() <= shell.height() - 39 && message->property("y").toReal() >= 19,
                    "long content expands within the overlay instead of growing outside the window");
                auto* footer = message->findChild<QQuickItem*>("dialogActions");
                check(footer && rect(footer).top() >= rect(scroll).bottom() - 1
                        && rect(footer).bottom() <= shell.height() - 19,
                    "dialog actions stay visible below the scrollable body even with very long text");
                auto* flick = qvariant_cast<QQuickItem*>(scroll->property("contentItem"));
                check(flick, "dialog has an interactive scroll viewport");
                const auto wheelPoint = rect(scroll).center();
                QWheelEvent wheel(wheelPoint, shell.mapToGlobal(wheelPoint.toPoint()), QPoint(0, -120), QPoint(0, -960),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedBySystem);
                QCoreApplication::sendEvent(&shell, &wheel);
                pump(100);
                check(flick->property("contentY").toReal() > 0, "dialog overflow remains accessible to wheel and trackpad scrolling");
                check(shell.grabWindow().save("/tmp/echo-layout-refactor/dialog-overflow.png"), "bounded dialog proof saved");
                QMetaObject::invokeMethod(message, "close");
                check(shell.rootObject()->findChild<QQuickWindow*>("miniPlayer") == nullptr, "mini player window is gone");
                check(qmlIssues.isEmpty(), "shared declarative layouts produce no QML warnings");
                qInfo() << "PASS" << passedChecks() << "shared layout contract checks";
                QCoreApplication::exit(0);
            });
            return app.exec();
}
