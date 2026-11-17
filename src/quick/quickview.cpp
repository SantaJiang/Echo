#include "quick/quickview.h"
#include "platform/platform.h"
#include "presentation/theme.h"
#include "quick/client.h"
#include "quick/playbackview.h"
#include "presentation/icons.h"
#include <QCloseEvent>
#include <QPainter>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <algorithm>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#endif

// Q_INIT_RESOURCE expands to a global symbol and must not sit inside any namespace.
static void initEchoResources()
{
    Q_INIT_RESOURCE(quick);
    Q_INIT_RESOURCE(playback_shaders);
    Q_INIT_RESOURCE(echo);
}

namespace echo {
void initializeEchoResources()
{
    initEchoResources();
}
} // namespace echo

namespace echo {
namespace {
    class Icons final : public QQuickImageProvider {
    public:
        Icons()
            : QQuickImageProvider(QQuickImageProvider::Pixmap)
        {
        }
        QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested) override
        {
            const int side = std::clamp(std::max(requested.width(), requested.height()), 16, 1024);
            if (size)
                *size = QSize(side, side);
            if (id.startsWith("cover/")) {
                QPixmap cover(side, side);
                QPainter painter(&cover);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.scale(side / 800.0, side / 800.0);
                QLinearGradient gradient(0, 0, 800, 800);
                gradient.setColorAt(0, QColor("#35305f"));
                gradient.setColorAt(.5, QColor("#b2678c"));
                gradient.setColorAt(1, QColor("#edba91"));
                painter.fillRect(QRect(0, 0, 800, 800), gradient);
                if (id == QStringLiteral("cover/background"))
                    return cover;
                painter.setPen(QColor("#ccffffff"));
                QFont font("Helvetica");
                font.setPixelSize(64);
                painter.setFont(font);
                painter.drawText(QRect(60, 60, 680, 250), Qt::AlignLeft, "ECHO\nSESSIONS");
                font.setPixelSize(27);
                painter.setFont(font);
                painter.drawText(QPoint(60, 730), "N I G H T F A L L");
                return cover;
            }
            const auto pieces = id.split(u'/');
            const UiTheme& ink = activeUiTheme();
            const QColor color = pieces.value(1) == "dark" ? ink.canvas
                : pieces.value(1) == "accent"              ? ink.accent
                                                           : ink.textPrimary;
            const QHash<QString, Icon> kinds { { "play", Icon::Play }, { "pause", Icon::Pause },
                { "previous", Icon::Previous }, { "next", Icon::Next }, { "heart", Icon::Heart },
                { "heartFilled", Icon::HeartFilled }, { "shuffle", Icon::Shuffle }, { "repeat", Icon::Repeat },
                { "queue", Icon::Queue }, { "volume", Icon::Volume }, { "songs", Icon::Songs },
                { "albums", Icon::Albums }, { "artists", Icon::Artists }, { "folders", Icon::Folders },
                { "playlist", Icon::Playlist }, { "search", Icon::Search }, { "plus", Icon::Plus },
                { "settings", Icon::Settings }, { "expand", Icon::Expand } };
            if (pieces.value(0) == "collapse" || pieces.value(0) == "back" || pieces.value(0) == "close") {
                QPixmap pix(side, side);
                pix.fill(Qt::transparent);
                QPainter p(&pix);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                if (pieces.value(0) == "back") {
                    p.translate(side, 0);
                    p.rotate(90);
                }
                if (pieces.value(0) == "close") {
                    p.drawLine(QPointF(side * .3, side * .3), QPointF(side * .7, side * .7));
                    p.drawLine(QPointF(side * .3, side * .7), QPointF(side * .7, side * .3));
                    return pix;
                }
                p.drawPolyline(QPolygonF {
                    QPointF(side * .25, side * .4), QPointF(side * .5, side * .65), QPointF(side * .75, side * .4) });
                return pix;
            }
            return icon(kinds.value(pieces.value(0), Icon::Songs), color, side).pixmap(side, side);
        }
    };
}
QuickView::QuickView(PlaybackView* playback, Client* client)
{
    m_customChrome = true;
    // A system backdrop can only show through an alpha-composited surface. This
    // is a static switch and must be armed before create().
    QQuickWindow::setDefaultAlphaBuffer(true);
    m_backdropAvailable = echo::supportsWindowBackdrop();
    m_glassWanted = client->preferences().value(QStringLiteral("glassSidebar")).toBool();
    const bool glass = m_backdropAvailable && m_glassWanted;
    // The platform layer cannot read the preference itself, so the request
    // travels on the window.
    setProperty("echoGlassWanted", glass);
#ifdef Q_OS_MACOS
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // Keep Cocoa's full-size content mode in Qt's own window state, so Qt
    // preserves it when maximizing or returning from native fullscreen.
    setFlags(Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
#endif
#else
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
#endif
    connect(this, &QWindow::windowStateChanged, this, &QuickView::chromeChanged);
    connect(this, &QWindow::visibilityChanged, this, &QuickView::chromeChanged);
    setTitle(QStringLiteral("Echo"));
    setMinimumSize(QSize(1040, 680));
    resize(1180, 800);
    // Linux has no DWM/Cocoa corner radius for a frameless window. A transparent
    // clear colour lets the QML mask show the desktop through the corners.
    // macOS and Windows stay opaque unless a backdrop is really available:
    // a transparent window without one renders black.
#ifdef Q_OS_LINUX
    setColor(Qt::transparent);
#else
    setColor(glass ? QColor(Qt::transparent) : activeUiTheme().canvas);
#endif
    setResizeMode(QQuickView::SizeRootObjectToView);
    engine()->addImageProvider(QStringLiteral("echo-icons"), new Icons);
    rootContext()->setContextProperty(QStringLiteral("playback"), playback);
    rootContext()->setContextProperty(QStringLiteral("client"), client);
    rootContext()->setContextProperty(QStringLiteral("chrome"), this);
    const QSize initialSize = size();
    create();
    setProperty("echoWindowBackdrop", applyQuickWindowChrome(this));
    // The preference asked for glass but the platform refused (Windows 10, or
    // the compositor's transparency switch is off): go back to an opaque window
    // before the first frame, otherwise the sidebar hole shows black.
    if (!sidebarBackdrop() && glass)
        setColor(activeUiTheme().canvas);
    connect(client, &Client::preferencesChanged, this, [this, client] {
        const bool wanted = client->preferences().value(QStringLiteral("glassSidebar")).toBool();
        if (wanted == m_glassWanted)
            return;
        m_glassWanted = wanted;
        const bool on = wanted && m_backdropAvailable;
        setProperty("echoGlassWanted", on);
        setColor(on ? QColor(Qt::transparent) : activeUiTheme().canvas);
        setProperty("echoWindowBackdrop", echo::setWindowBackdrop(this, on));
        emit chromeChanged();
    });
    // Expanded Cocoa content can alter the initial native geometry during
    // creation. Reassert the requested size in Qt logical coordinates.
    resize(initialSize);
    connect(this, &QWindow::visibilityChanged, playback, [playback](QWindow::Visibility visibility) {
        const bool active = visibility != QWindow::Hidden && visibility != QWindow::Minimized;
        playback->setPresentationActive(active);
    });
    setSource(QUrl(QStringLiteral("qrc:/echo/quick/Shell.qml")));
}
void QuickView::beginWindowMove()
{
#ifndef Q_OS_MACOS
    if (!fullscreen())
        startSystemMove();
#endif
    // Cocoa's native press/drag monitor also handles an inactive window's first drag.
}
void QuickView::resizeWindow(int edges)
{
    if (!maximized() && !fullscreen())
        startSystemResize(Qt::Edges(edges));
}
void QuickView::toggleMaximized()
{
    if (fullscreen())
        return;
    maximized() ? showNormal() : showMaximized();
}
void QuickView::hideToBackground()
{
    if (m_hidePending) return;
    m_hidePending = true;
    const auto revision = ++m_hideRevision;
    const QPointer<QuickView> guarded(this);
    const auto finish = [guarded, revision] {
        if (!guarded || !guarded->m_hidePending || guarded->m_hideRevision != revision) return;
        guarded->m_hidePending = false;
        guarded->hide();
        emit guarded->hiddenToTray();
    };
    if (!deferHideUntilFullscreenExit(this, finish)) finish();
}

void QuickView::showFromBackground()
{
    // A tray click during the exit animation cancels its pending hide.
    m_hidePending = false;
    ++m_hideRevision;
    if (visibility() == QWindow::Minimized) {
        showNormal();
    } else {
        // QWindow::show() selects the platform's default window state and can
        // restore a maximized/fullscreen window when merely opening settings.
        setVisible(true);
    }
    raise();
    requestActivate();
}

bool QuickView::event(QEvent* event)
{
    if (event->type() == QEvent::Close && !QCoreApplication::closingDown()) {
        static_cast<QCloseEvent*>(event)->ignore();
        hideToBackground();
        return true;
    }
    return QQuickView::event(event);
}
#ifdef Q_OS_WIN
bool QuickView::nativeEvent(const QByteArray& type, void* message, qintptr* result)
{
    if (!m_customChrome || (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG"))
        return QQuickView::nativeEvent(type, message, result);
    const auto* msg = static_cast<MSG*>(message);
    const HWND hwnd = msg->hwnd;
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // Maximized frameless windows must fit the work area, including a side taskbar.
        if (IsZoomed(hwnd)) {
            MONITORINFO monitor { sizeof(MONITORINFO) };
            if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0] = monitor.rcWork;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST && !fullscreen()) {
        RECT frame;
        GetWindowRect(hwnd, &frame);
        const int x = GET_X_LPARAM(msg->lParam) - frame.left;
        const int y = GET_Y_LPARAM(msg->lParam) - frame.top;
        const int w = frame.right - frame.left;
        const int h = frame.bottom - frame.top;
        const qreal dpr = devicePixelRatio();
        const int border = qRound(6 * dpr);
        if (!IsZoomed(hwnd)) {
            const bool left = x < border, right = x >= w - border;
            const bool top = y < border, bottom = y >= h - border;
            if (left || right || top || bottom) {
                *result = top ? (left           ? HTTOPLEFT
                                        : right ? HTTOPRIGHT
                                                : HTTOP)
                    : bottom  ? (left           ? HTBOTTOMLEFT
                                        : right ? HTBOTTOMRIGHT
                                                : HTBOTTOM)
                    : left    ? HTLEFT
                              : HTRIGHT;
                return true;
            }
        }
        // The OS sees the maximize button for Windows 11's Snap Layouts hover.
        if (y >= qRound(4 * dpr) && y < qRound(44 * dpr) && x >= w - qRound(100 * dpr) && x < w - qRound(54 * dpr)) {
            *result = HTMAXBUTTON;
            return true;
        }
        // 内容从标题栏下沿（48）开始，整条标题栏都是空的，
        // 除窗口按钮外都可以拖动。
        if (x < w - qRound(146 * dpr) && y < qRound(48 * dpr)) {
            *result = HTCAPTION;
            return true;
        }
        *result = HTCLIENT;
        return true;
    }
    if (msg->message == WM_NCMOUSEMOVE && msg->wParam == HTMAXBUTTON) {
        if (!m_maximizeHovered) {
            m_maximizeHovered = true;
            emit chromeChanged();
            TRACKMOUSEEVENT track { sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
            TrackMouseEvent(&track);
        }
    }
    if (msg->message == WM_NCMOUSELEAVE) {
        m_maximizeHovered = false;
        m_maximizePressed = false;
        emit chromeChanged();
    }
    if (msg->message == WM_NCLBUTTONDOWN && msg->wParam == HTMAXBUTTON) {
        m_maximizePressed = true;
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCLBUTTONUP && m_maximizePressed) {
        m_maximizePressed = false;
        if (msg->wParam == HTMAXBUTTON)
            toggleMaximized();
        *result = 0;
        return true;
    }
    // Turning the system "Transparency effects" switch off silently disables
    // Mica while DwmSetWindowAttribute keeps returning S_OK, so re-probe on
    // every relevant broadcast and only touch DWM when the answer changed.
    if (msg->message == WM_SETTINGCHANGE || msg->message == WM_DWMCOMPOSITIONCHANGED
        || msg->message == WM_THEMECHANGED) {
        const bool active = echo::setWindowBackdrop(this, property("echoGlassWanted").toBool());
        if (active != sidebarBackdrop()) {
            setColor(active ? QColor(Qt::transparent) : activeUiTheme().canvas);
            setProperty("echoWindowBackdrop", active);
            emit chromeChanged();
        }
    }
    return QQuickView::nativeEvent(type, message, result);
}
#endif

}
