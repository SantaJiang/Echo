#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"
#include "ui/playerbar.h"
#include "ui/miniplayer.h"
#include "ui/settingsdialog.h"
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
    QApplication application(argc, argv);
    echo::useDarkTheme();
    echo::useDarkAppearance();
    QCoreApplication::setOrganizationName(QStringLiteral("EchoShot"));
    QCoreApplication::setApplicationName(QStringLiteral("EchoShot"));
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
    QObject::connect(&library, &echo::Library::scanFinished, &loop, &QEventLoop::quit);
    library.addRoot(root);
    loop.exec();

    echo::AudioEngine engine;
    echo::Player player(&library, &engine);
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
    bar->setTrack(tracks.front(), {});
    bar->setFormatText(QStringLiteral("44.1 kHz · 32-bit float → 48.0 kHz"));
    bar->setPlaying(true);
    bar->setPosition(8000, 143000);
    grab(&window, out, QStringLiteral("playing"));
    auto* format = bar->findChild<QLabel*>(QStringLiteral("audioFormat"));
    auto* seek = bar->findChild<QSlider*>(QStringLiteral("playbackSeek"));
    check(std::abs(format->mapTo(bar, format->rect().center()).y() - seek->mapTo(bar, seek->rect().center()).y()) <= 1,
          "format and progress share a horizontal centerline");
    window.resize(1040, 640);
    grab(&window, out, QStringLiteral("minimum"));
    check(bar->width() <= window.centralWidget()->width() - 260, "player fits minimum width");
    check(seek->width() >= 120, "seek remains usable at minimum width");
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
    auto* mini = window.findChild<echo::MiniPlayer*>();
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
    return 0;
}
