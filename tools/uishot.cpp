#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QTimer>

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
    return 0;
}
