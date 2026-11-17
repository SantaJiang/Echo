#include "platform/platform.h"

#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QCursor>
#include <QStyleHints>
#include <QWindow>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
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

void useDarkAppearance() {}

void installApplicationMenu(QObject*, const QVariantList&, const std::function<void(int)>&) {}

void applyQuickWindowChrome(QWindow* window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    // Keep the system resize/snap capabilities, while Quick paints the entire client area.
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE,
        style | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const MARGINS shadow { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    // DWMWA_WINDOW_CORNER_PREFERENCE; older Windows versions safely ignore it.
    const int corners = 2;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}


void installMediaIntegration(QWindow* window, const std::function<void(MediaCommand, qint64)>& handler)
{
    g_handler = handler;
    if (!g_filter) {
        g_filter = new MediaFilter();
        qGuiApp->installNativeEventFilter(g_filter);
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RegisterHotKey(hwnd, 1, 0, VK_MEDIA_PLAY_PAUSE);
    RegisterHotKey(hwnd, 2, 0, VK_MEDIA_NEXT_TRACK);
    RegisterHotKey(hwnd, 3, 0, VK_MEDIA_PREV_TRACK);
}

namespace {
struct Tray {
    NOTIFYICONDATAW data{};
    std::function<void()> open;
    std::function<void(const QPoint&)> context;
    UINT taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    static LRESULT CALLBACK process(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* tray = reinterpret_cast<Tray*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (tray && message == tray->taskbar) Shell_NotifyIconW(NIM_ADD, &tray->data);
        if (tray && message == WM_APP + 42) {
            if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) tray->open();
            else if (l == WM_RBUTTONUP) tray->context(QCursor::pos());
            return 0;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};
}
void installTrayIcon(QObject* owner, const std::function<void()>& open, const std::function<void()>& settings,
    const std::function<void(const QPoint&)>& context)
{
    Q_UNUSED(settings);
    WNDCLASSW cls{}; cls.lpfnWndProc = Tray::process; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"EchoTray";
    RegisterClassW(&cls);
    auto* tray = new Tray; tray->open = open; tray->context = context;
    HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"Echo", 0, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
    tray->data.cbSize = sizeof(NOTIFYICONDATAW); tray->data.hWnd = hwnd; tray->data.uID = 1;
    tray->data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray->data.uCallbackMessage = WM_APP + 42;
    tray->data.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_ECHO");
    if (!tray->data.hIcon) tray->data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    lstrcpynW(tray->data.szTip, L"Echo", ARRAYSIZE(tray->data.szTip)); Shell_NotifyIconW(NIM_ADD, &tray->data);
    QObject::connect(owner, &QObject::destroyed, [tray, hwnd] { Shell_NotifyIconW(NIM_DELETE, &tray->data); DestroyWindow(hwnd); delete tray; });
}

void updateSystemNowPlaying(const Track&, const QImage&, qint64, qint64, bool, bool) {}

} // namespace echo
