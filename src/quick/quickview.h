#pragma once
#include <QQuickView>
namespace echo {
class PlaybackView;
class Client;
class QuickView : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(bool maximized READ maximized NOTIFY chromeChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY chromeChanged)
    Q_PROPERTY(bool maximizeHovered READ maximizeHovered NOTIFY chromeChanged)
public:
    explicit QuickView(PlaybackView* playback, Client* client = nullptr);
    bool maximized() const { return windowState() == Qt::WindowMaximized; }
    bool fullscreen() const { return windowState() == Qt::WindowFullScreen; }
    bool maximizeHovered() const { return m_maximizeHovered; }
    Q_INVOKABLE void beginWindowMove();
    Q_INVOKABLE void resizeWindow(int edges);
    Q_INVOKABLE void minimizeWindow() { showMinimized(); }
    Q_INVOKABLE void toggleMaximized();
    Q_INVOKABLE void closeWindow() { close(); }
    void hideToBackground();
    void showFromBackground();
signals:
    void chromeChanged();
    void hiddenToTray();

protected:
    bool event(QEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
#endif
private:
    bool m_hidePending = false;
    quint64 m_hideRevision = 0;
    bool m_customChrome = false;
    bool m_maximizeHovered = false;
    bool m_maximizePressed = false;
};
}
