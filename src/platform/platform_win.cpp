#include "platform/platform.h"

#include <QAbstractNativeEventFilter>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QStyleHints>
#include <QWindow>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// Pre-22H2 Windows 11 only knows the undocumented Mica flag.
#define DWMWA_MICA_EFFECT 1029
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#define DWMSBT_NONE 1
#define DWMSBT_MAINWINDOW 2

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

bool platformIsWindows()
{
    return QGuiApplication::platformName() == QLatin1String("windows");
}

// RtlGetVersion is the only build check a compatibility manifest cannot fake,
// which is exactly why GetVersionEx is useless for detecting Windows 11.
struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    wchar_t servicePack[128];
};

DWORD windowsBuild()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll
        ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))
        : nullptr;
    if (!rtlGetVersion)
        return 0;
    OsVersionInfo info {};
    info.size = sizeof(info);
    return rtlGetVersion(&info) == 0 ? info.build : 0;
}

// Settings > Personalisation > Colours > Transparency effects. When it is off
// DWM silently ignores the backdrop and an alpha window renders black, so the
// QML hole has to be closed again.
bool transparencyEffectsEnabled()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency",
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status != ERROR_SUCCESS || value != 0;
}

bool applyBackdrop(HWND hwnd, bool wanted)
{
    const DWORD build = windowsBuild();
    if (!wanted || build < 22000 || !transparencyEffectsEnabled()) {
        const int none = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
        const BOOL off = FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, &off, sizeof(off));
        return false;
    }
    if (build >= 22621) {
        const int mica = DWMSBT_MAINWINDOW;
        return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &mica, sizeof(mica)));
    }
    // 22000 .. 22620: only the undocumented flag exists. It reports S_OK even
    // when DWM ignores the request, so a true result means "requested".
    const BOOL on = TRUE;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, &on, sizeof(on)));
}

} // namespace

bool supportsWindowBackdrop()
{
    return platformIsWindows() && windowsBuild() >= 22000 && transparencyEffectsEnabled();
}

bool setWindowBackdrop(QWindow* window, bool enabled)
{
    if (!window || !platformIsWindows())
        return false;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const bool active = applyBackdrop(hwnd, enabled);
    // A full-sheet glass window looks better without the highlight frame line;
    // the opaque theme still relies on the default edge.
    const DWORD color = active ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));
    return active;
}

void useDarkAppearance() {}

void installApplicationMenu(QObject*, const QVariantList&, const std::function<void(int)>&) {}

bool applyQuickWindowChrome(QWindow* window)
{
    if (!platformIsWindows())
        return false;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    // Turn the frameless popup into an overlapped window: DWM withholds both
    // the shadow and every backdrop material from WS_POPUP windows. The client
    // area is still painted entirely by Quick through WM_NCCALCSIZE.
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & (WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP)) {
        // Both flags make DWM bypass the redirection surface, so no backdrop
        // can ever appear. Only surfaced to make a blank sidebar diagnosable.
        qWarning("echo.window: layered or redirected window, the system backdrop will not show");
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // The whole client area becomes one sheet of glass. This replaces the 1px
    // margin that only existed to draw a shadow border: the shadow survives
    // because WS_THICKFRAME is still present, and both are full-replacement
    // calls, so they cannot be combined.
    const MARGINS sheet { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &sheet);
    // Order matters: re-creating the frame resets the backdrop, so the frame
    // change has to happen first.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // DWMWA_WINDOW_CORNER_PREFERENCE; older Windows versions safely ignore it.
    const int corners = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    return setWindowBackdrop(window, window->property("echoGlassWanted").toBool());
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
