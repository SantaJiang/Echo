#include "quick/systemintegration.h"

#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "quick/quickview.h"

#include <QCursor>
#include <QGuiApplication>
#include <QShortcut>
#include <QTimer>
#include <algorithm>

namespace echo {

SystemIntegration::SystemIntegration(QObject* parent)
    : QObject(parent)
{
}

void SystemIntegration::attach(QuickView* window, const Hooks& hooks)
{
    m_window = window;
    m_hooks = hooks;
    window->setProperty("echoMainWindow", true);
    qGuiApp->setQuitOnLastWindowClosed(false);
    installTrayIcon(this, [this] { showMainWindow(); }, [this] { if (m_hooks.showSettings) m_hooks.showSettings(); },
        [this](const QPoint& position) { if (m_hooks.trayMenu) m_hooks.trayMenu(position); });
    installApplicationMenu(this, m_hooks.applicationMenu ? m_hooks.applicationMenu() : QVariantList {},
        [this](int token) { if (m_hooks.triggerMenu) m_hooks.triggerMenu(token); });
    installMediaIntegration(window, [this](MediaCommand command, qint64 value) {
        if (!m_hooks.player)
            return;
        switch (command) {
        case MediaCommand::Toggle:
            m_hooks.player->toggle();
            break;
        case MediaCommand::Next:
            m_hooks.player->next();
            break;
        case MediaCommand::Previous:
            m_hooks.player->previous();
            break;
        case MediaCommand::Seek:
            m_hooks.player->seek(value);
            break;
        }
    });
    auto shortcut = [this, window](const QKeySequence& key, std::function<void()> call) {
        auto* binding = new QShortcut(key, window);
        connect(binding, &QShortcut::activated, this, std::move(call));
    };
    shortcut(QKeySequence::Quit, [] { quitApplication(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_O), [this] { if (m_hooks.addMusicFolder) m_hooks.addMusicFolder(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { if (m_hooks.showSettings) m_hooks.showSettings(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { if (m_hooks.createPlaylist) m_hooks.createPlaylist(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { if (m_hooks.focusSearch) m_hooks.focusSearch(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Right), [this] { if (m_hooks.player) m_hooks.player->next(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Left), [this] { if (m_hooks.player) m_hooks.player->previous(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), [this] {
        if (m_hooks.player)
            m_hooks.player->setVolume(std::min(1.f, m_hooks.player->volume() + .05f));
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), [this] {
        if (m_hooks.player)
            m_hooks.player->setVolume(std::max(0.f, m_hooks.player->volume() - .05f));
    });
    auto* clock = new QTimer(this);
    clock->setInterval(250);
    connect(clock, &QTimer::timeout, this, [this] { sync(false); });
    clock->start();
    connect(m_hooks.library->artwork(), &ArtworkCache::ready, this, [this](const QString& hash, int size) {
        const auto track = m_hooks.player->currentTrack();
        if (hash == track.artworkHash && size == 160) {
            m_artwork = m_hooks.library->artwork()->pixmap(hash, size).toImage();
            sync(true);
        }
    });
    connect(m_hooks.player, &Player::changed, this, [this] { sync(false); });
    connect(m_hooks.engine, &AudioEngine::positionChanged, this, [this] { sync(true); });
    connect(window, &QuickView::hiddenToTray, m_hooks.player, &Player::saveSession);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_window && !m_window->isVisible() && (!m_mini || !m_mini->isVisible())
            && !(m_hooks.dialogOpen && m_hooks.dialogOpen()))
            showMainWindow();
    });
    sync(true);
}

void SystemIntegration::setMiniWindow(QWindow* window)
{
    m_mini = window;
    if (window) {
        window->setProperty("echoMiniPlayer", true);
        applyMiniWindowChrome(window);
    }
}

void SystemIntegration::showMainWindow()
{
    if (m_mini)
        m_mini->hide();
    if (!m_window)
        return;
    m_window->showFromBackground();
}

void SystemIntegration::showMini()
{
    if (m_mini) {
        m_mini->show();
        m_mini->raise();
        m_mini->requestActivate();
    }
    if (m_window)
        m_window->hideToBackground();
}

QPointF SystemIntegration::cursorPosition() const
{
    return m_window ? m_window->mapFromGlobal(QCursor::pos()) : QPointF();
}

void SystemIntegration::sync(bool force)
{
    if (!m_hooks.player || !m_hooks.library)
        return;
    const auto track = m_hooks.player->currentTrack();
    if (m_artworkHash != track.artworkHash) {
        m_artworkHash = track.artworkHash;
        m_artwork = m_hooks.library->artwork()->pixmap(track.artworkHash, 160).toImage();
    }
    updateSystemNowPlaying(track, m_artwork, m_hooks.player->positionMs(), track.durationMs,
        m_hooks.player->state() == AudioEngine::State::Playing, force);
}

} // namespace echo
