#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <functional>

class QWindow;
class QQuickView;

namespace echo {
class AudioEngine;
class Library;
class Player;
class QuickView;

// Tray, menu bar, media keys, shortcuts and system now-playing.
class SystemIntegration : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<void()> showSettings;
        std::function<void()> addMusicFolder;
        std::function<void()> createPlaylist;
        std::function<void()> focusSearch;
        std::function<void(int)> triggerMenu;
        std::function<QVariantList()> applicationMenu;
        std::function<void(const QPoint&)> trayMenu;
        std::function<bool()> dialogOpen;
        Player* player = nullptr;
        Library* library = nullptr;
        AudioEngine* engine = nullptr;
    };

    explicit SystemIntegration(QObject* parent = nullptr);
    void attach(QuickView* window, const Hooks& hooks);
    void setMiniWindow(QWindow* window);
    void showMainWindow();
    void showMini();
    [[nodiscard]] QuickView* window() const { return m_window; }
    [[nodiscard]] QWindow* mini() const { return m_mini; }
    [[nodiscard]] QPointF cursorPosition() const;

private:
    void sync(bool force);
    QPointer<QuickView> m_window;
    QPointer<QWindow> m_mini;
    Hooks m_hooks;
    QString m_artworkHash;
    QImage m_artwork;
};

} // namespace echo
