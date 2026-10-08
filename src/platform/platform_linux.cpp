#include "platform/platform.h"

#include <QWidget>

namespace echo {

void useDarkAppearance() {}

void applyWindowChrome(QWidget*) {}

void installSidebarEffect(QWidget*) {}

} // namespace echo

#ifndef ECHO_HAS_MPRIS

namespace echo {

void installMediaIntegration(QWidget*, const std::function<void(MediaCommand, qint64)>&) {}

void updateSystemNowPlaying(const Track&, const QImage&, qint64, qint64, bool) {}

} // namespace echo

#else

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QApplication>

namespace echo {

namespace {

std::function<void(MediaCommand, qint64)> g_handler;
qint64 g_positionMs = 0;
bool g_playing = false;
QString g_title;
QString g_artist;
QString g_album;
qint64 g_durationMs = 0;
QObject* g_object = nullptr;

} // namespace

class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktop CONSTANT)

public:
    explicit MprisRoot(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Echo"); }
    QString desktop() const { return QStringLiteral("echo"); }

public slots:
    void Quit() { qApp->quit(); }
    void Raise()
    {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            widget->show();
            widget->raise();
            widget->activateWindow();
        }
    }
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(QVariantMap Metadata READ metadata)

public:
    explicit MprisPlayer(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    QString playbackStatus() const { return g_playing ? QStringLiteral("Playing") : QStringLiteral("Paused"); }
    QString loopStatus() const { return QStringLiteral("None"); }
    double rate() const { return 1; }
    qlonglong position() const { return g_positionMs * 1000; }
    bool canControl() const { return true; }
    QVariantMap metadata() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("mpris:trackid"), QStringLiteral("/echo/track"));
        map.insert(QStringLiteral("xesam:title"), g_title);
        map.insert(QStringLiteral("xesam:artist"), QStringList {g_artist});
        map.insert(QStringLiteral("xesam:album"), g_album);
        map.insert(QStringLiteral("mpris:length"), g_durationMs * 1000);
        return map;
    }

public slots:
    void PlayPause()
    {
        if (g_handler) {
            g_handler(MediaCommand::Toggle, 0);
        }
    }
    void Play() { PlayPause(); }
    void Pause() { PlayPause(); }
    void Next()
    {
        if (g_handler) {
            g_handler(MediaCommand::Next, 0);
        }
    }
    void Previous()
    {
        if (g_handler) {
            g_handler(MediaCommand::Previous, 0);
        }
    }
    void Seek(qlonglong offset)
    {
        if (g_handler) {
            g_handler(MediaCommand::Seek, g_positionMs + offset / 1000);
        }
    }
    void SetPosition(const QDBusObjectPath&, qlonglong microseconds)
    {
        if (g_handler) {
            g_handler(MediaCommand::Seek, microseconds / 1000);
        }
    }
};

void installMediaIntegration(QWidget*, const std::function<void(MediaCommand, qint64)>& handler)
{
    g_handler = handler;
    if (g_object) {
        return;
    }
    g_object = new QObject(qApp);
    new MprisRoot(g_object);
    new MprisPlayer(g_object);
    auto bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.echo"));
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), g_object, QDBusConnection::ExportAdaptors);
}

void updateSystemNowPlaying(const Track& track, const QImage&, qint64 positionMs, qint64 durationMs, bool playing)
{
    const bool changed = g_playing != playing || g_title != track.displayTitle();
    g_positionMs = positionMs;
    g_durationMs = durationMs;
    g_playing = playing;
    g_title = track.displayTitle();
    g_artist = track.displayArtist();
    g_album = track.displayAlbum();
    if (!changed || !g_object) {
        return;
    }
    QVariantMap metadata;
    metadata.insert(QStringLiteral("xesam:title"), g_title);
    metadata.insert(QStringLiteral("xesam:artist"), QStringList {g_artist});
    metadata.insert(QStringLiteral("xesam:album"), g_album);
    metadata.insert(QStringLiteral("mpris:length"), g_durationMs * 1000);
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    message << QStringLiteral("org.mpris.MediaPlayer2.Player")
            << QVariantMap {{QStringLiteral("PlaybackStatus"), g_playing ? QStringLiteral("Playing") : QStringLiteral("Paused")},
                            {QStringLiteral("Metadata"), metadata}}
            << QStringList();
    QDBusConnection::sessionBus().send(message);
}

} // namespace echo

#include "platform_linux.moc"

#endif
