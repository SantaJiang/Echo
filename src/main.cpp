#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "quick/client.h"
#include "quick/playbackview.h"
#include "quick/quickview.h"
#include "ui/theme.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <functional>
#include <memory>

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
    QGuiApplication application(argc, argv);
    Q_INIT_RESOURCE(quick);
    Q_INIT_RESOURCE(echo);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    echo::useDarkTheme();
    echo::useDarkAppearance();
    QCoreApplication::setOrganizationName(QStringLiteral("Echo"));
    QCoreApplication::setApplicationName(QStringLiteral("Echo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.5"));
    QIcon appIcon;
    for (const int size : { 32, 64, 128, 256, 512 }) {
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

    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("files", QStringLiteral("音乐文件或资料库文件夹"), "[files...]");
    parser.process(application);
    const auto paths = parser.positionalArguments();
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
        QQmlApplicationEngine errorView;
        errorView.rootContext()->setContextProperty("startupMessage", error.isEmpty() ? QStringLiteral("无法打开资料库") : error);
        errorView.load(QUrl("qrc:/echo/quick/StartupError.qml"));
        if (!errorView.rootObjects().isEmpty()) application.exec();
        return 1;
    }
    echo::AudioEngine engine;
    echo::Player player(&library, &engine);
    player.restoreSession();
    echo::PlaybackView playback(&library, &player, &engine);
    echo::Client client(&library, &player, &engine);
    echo::QuickView quick(&playback, &client);
    if (quick.status() != QQuickView::Ready) {
        qCritical("无法加载 Qt Quick 界面，请检查 QML 运行依赖。"); return 1;
    }
    client.attach(&quick);
    auto openPaths = [&](const QStringList& files) { client.openPaths(files); };
    auto showWindow = [&] { client.showMainWindow(); };

    QLocalServer::removeServer(QStringLiteral("echo-player"));
    QLocalServer server;
    server.listen(QStringLiteral("echo-player"));
    QObject::connect(&server, &QLocalServer::newConnection, &application, [&server, &openPaths, &showWindow] {
        while (QLocalSocket* client = server.nextPendingConnection()) {
            client->waitForReadyRead(300);
            const QStringList incoming = QString::fromUtf8(client->readAll()).split(u'\n', Qt::SkipEmptyParts);
            openPaths(incoming);
            showWindow();
            client->deleteLater();
        }
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &player, &echo::Player::saveSession);
    if (!paths.isEmpty()) {
        openPaths(paths);
    }
    showWindow();
    return application.exec();
}
