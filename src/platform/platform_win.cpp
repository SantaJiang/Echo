#include "platform/platform.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QStyleHints>
#include <QWidget>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace echo {
namespace {

std::function<void(MediaCommand, qint64)> g_handler;

class MediaFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override
    {
        Q_UNUSED(result);
        if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") {
            return false;
        }
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && g_handler) {
            if (msg->wParam == 1) {
                g_handler(MediaCommand::Toggle, 0);
            } else if (msg->wParam == 2) {
                g_handler(MediaCommand::Next, 0);
            } else if (msg->wParam == 3) {
                g_handler(MediaCommand::Previous, 0);
            }
            return true;
        }
        if (msg->message == WM_APPCOMMAND && g_handler) {
            switch (GET_APPCOMMAND_LPARAM(msg->lParam)) {
            case APPCOMMAND_MEDIA_PLAY_PAUSE:
            case APPCOMMAND_MEDIA_PLAY:
            case APPCOMMAND_MEDIA_PAUSE:
                g_handler(MediaCommand::Toggle, 0);
                return true;
            case APPCOMMAND_MEDIA_NEXTTRACK:
                g_handler(MediaCommand::Next, 0);
                return true;
            case APPCOMMAND_MEDIA_PREVIOUSTRACK:
                g_handler(MediaCommand::Previous, 0);
                return true;
            default:
                break;
            }
        }
        return false;
    }
};

MediaFilter* g_filter = nullptr;

} // namespace

void applyWindowChrome(QWidget* window)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const int backdrop = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

void installSidebarEffect(QWidget*) {}

void installMediaIntegration(QWidget* window, const std::function<void(MediaCommand, qint64)>& handler)
{
    g_handler = handler;
    if (!g_filter) {
        g_filter = new MediaFilter();
        qApp->installNativeEventFilter(g_filter);
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RegisterHotKey(hwnd, 1, 0, VK_MEDIA_PLAY_PAUSE);
    RegisterHotKey(hwnd, 2, 0, VK_MEDIA_NEXT_TRACK);
    RegisterHotKey(hwnd, 3, 0, VK_MEDIA_PREV_TRACK);
}

void updateSystemNowPlaying(const Track&, const QImage&, qint64, qint64, bool) {}

} // namespace echo
