#include "library/library.h"
#include "playback/player.h"
#include "test_support/support.h"
#include "quick/client.h"
#include "quick/playbackview.h"
#include "quick/quickview.h"
#include "presentation/theme.h"
#include "quicktest_harness.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEnterEvent>
#include <QFile>
#include <QPainter>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickStyle>
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
#include <limits>
#include <thread>

namespace {
QStringList& qmlIssues = echo::quicktest::issues();
using echo::test::SilentOutput;
using echo::test::check;
using echo::test::passedChecks;
using echo::test::waitFor;
void messages(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type == QtWarningMsg && message.contains("qrc:/echo/quick"))
        qmlIssues << message;
    fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
}
using echo::quicktest::pump;
void wave(const QString& path)
{
    echo::test::writePcm16Wave(path, 20000);
}
}
int main(int argc, char** argv)
{
    QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
    QGuiApplication app(argc, argv);
    echo::initializeEchoResources();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
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
        echo::Client client(&library, &player, &engine);
        echo::QuickView window(&playback, &client);
        check(window.status() == QQuickView::Ready, "QML loads");
        if (app.arguments().contains("--qml-smoke")) {
            check(waitFor([&] { return !client.page().value("busy").toBool(); }), "headless library model loads");
            check(qmlIssues.isEmpty(), "headless QML has no warnings");
            QTimer::singleShot(0, &client, [&client] { client.action("quit"); });
            check(app.exec() == 0, "explicit quit exits the application loop");
            qInfo() << "PASS" << passedChecks() << "headless QML startup checks; GPU interaction suite runs separately";
            return 0;
        }
        echo::quicktest::Harness harness{app, temp, library, error, ids, tags, otherIds, sourceId, probe, engine, player, playback, client, window, coverPath};
        if (app.arguments().contains("--floating-queue-test") || app.arguments().contains("--layout-contract-test") || app.arguments().contains("--album-resize-test") || app.arguments().contains("--ui-motion-review") || app.arguments().contains("--window-stability-review"))
            return echo::quicktest::runLayoutContract(harness);
        if (app.arguments().contains("--background-test"))
            return echo::quicktest::runBackgroundTest(harness);
        if (int code = echo::quicktest::runOptionalScenes(harness); code >= 0)
            return code;
        return echo::quicktest::runInteractiveSuite(harness);
    }
}
