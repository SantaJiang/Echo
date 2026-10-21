#include "ui/dialogs.h"
#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStandardPaths>

namespace {

void writeLog(QtMsgType, const QMessageLogContext&, const QString& message)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    const QString path = directory + QStringLiteral("/echo.log");
    if (QFileInfo(path).size() > 2 * 1024 * 1024) {
        QFile::remove(path);
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(message.toUtf8());
        file.write("\n");
    }
}

} // namespace

int main(int argc, char** argv)
{
#ifdef ECHO_QT_PLUGINS
    // Packaged macOS plugins must use the Qt frameworks inside the same bundle.
    // Keep the development fallback only when no bundled platform plugin exists.
    const QDir executableDirectory(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());
    if (!executableDirectory.exists(QStringLiteral("../PlugIns/platforms"))) {
        QCoreApplication::addLibraryPath(QString::fromUtf8(ECHO_QT_PLUGINS));
    }
#endif
    QApplication application(argc, argv);
    echo::useDarkTheme();
    echo::useDarkAppearance();
    QCoreApplication::setOrganizationName(QStringLiteral("Echo"));
    QCoreApplication::setApplicationName(QStringLiteral("Echo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.2"));
    QIcon appIcon;
    for (const int size : {32, 64, 128, 256, 512}) {
        appIcon.addFile(QStringLiteral(":/icons/echo-%1.png").arg(size));
    }
    application.setWindowIcon(appIcon);
    qInstallMessageHandler(writeLog);

    QFont font;
#if defined(Q_OS_MACOS)
    font.setFamily(QStringLiteral(".AppleSystemUIFont"));
#elif defined(Q_OS_WIN)
    font.setFamily(QStringLiteral("Segoe UI"));
#else
    font.setFamily(QStringLiteral("Noto Sans"));
#endif
    font.setPointSize(13);
    application.setFont(font);

    QStringList paths;
    for (int index = 1; index < argc; ++index) {
        paths.push_back(QString::fromLocal8Bit(argv[index]));
    }
    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("echo-player"));
    if (socket.waitForConnected(200)) {
        socket.write(paths.join(u'\n').toUtf8());
        socket.waitForBytesWritten(500);
        return 0;
    }

    echo::Library library;
    QString error;
    if (!library.open(&error)) {
        echo::MessageDialog::critical(nullptr, QStringLiteral("Echo"), error.isEmpty() ? QStringLiteral("无法打开资料库") : error);
        return 1;
    }
    echo::AudioEngine engine;
    echo::Player player(&library, &engine);
    player.restoreSession();
    echo::MainWindow window(&library, &player, &engine);

    QLocalServer::removeServer(QStringLiteral("echo-player"));
    QLocalServer server;
    server.listen(QStringLiteral("echo-player"));
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
        while (QLocalSocket* client = server.nextPendingConnection()) {
            client->waitForReadyRead(300);
            const QStringList incoming = QString::fromUtf8(client->readAll()).split(u'\n', Qt::SkipEmptyParts);
            window.openPaths(incoming);
            window.showMainWindow();
            client->deleteLater();
        }
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &player, &echo::Player::saveSession);
    if (!paths.isEmpty()) {
        window.openPaths(paths);
    }
    window.show();
    return application.exec();
}
