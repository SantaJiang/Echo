#include "ui/miniplayer.h"

#include "platform/platform.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QSignalBlocker>
#include <QWindow>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>
#include <limits>
#include <algorithm>

namespace echo {
namespace {
class MiniSeekSlider : public QSlider {
public:
    explicit MiniSeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {}
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setSliderDown(true);
        moveTo(event->position().x());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (isSliderDown())
            moveTo(event->position().x());
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->position().x());
            setSliderDown(false);
        }
        event->accept();
    }
private:
    void moveTo(qreal x)
    {
        constexpr int handle = 8;
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(), maximum(),
            qBound(0, qRound(x) - handle / 2, width() - handle), qMax(1, width() - handle)));
    }
};
} // namespace


MiniPlayer::MiniPlayer(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("Echo 迷你播放器"));
    setObjectName(QStringLiteral("miniPlayer"));
    setProperty("echoMiniPlayer", true);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(440, 100);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 10, 12, 8);
    outer->setSpacing(4);
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    m_art = new QLabel(this);
    m_art->setFixedSize(60, 60);
    m_art->setPixmap(placeholderCover(60));
    auto* meta = new QVBoxLayout();
    meta->setContentsMargins(0, 0, 0, 0);
    meta->setSpacing(4);
    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setStyleSheet(QStringLiteral("color: #F5F5F7; font-size: 13px; font-weight: 600;"));
    m_artist = new QLabel(this);
    m_artist->setStyleSheet(QStringLiteral("color: #98989D; font-size: 12px;"));
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_artist->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    for (auto* label : {m_art, m_title, m_artist}) label->setAttribute(Qt::WA_TransparentForMouseEvents);
    meta->addStretch();
    meta->addWidget(m_title);
    meta->addWidget(m_artist);
    meta->addStretch();
    auto make = [this](Icon kind, const QString& tip, int size) {
        auto* button = new QToolButton(this);
        button->setIcon(icon(kind, echo::palette().text, size));
        button->setIconSize(QSize(size, size));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(32, 32);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: none; border-radius: 16px; } QToolButton:hover { background: #3A3A3C; } QToolButton:pressed { background: #48484A; }"));
        return button;
    };
    m_favorite = make(Icon::Heart, QStringLiteral("添加到我喜欢的"), 18);
    m_favorite->setObjectName(QStringLiteral("favoriteButton"));
    m_favorite->setCursor(Qt::PointingHandCursor);
    setFavorite(false, false);
    connect(m_favorite, &QToolButton::toggled, this, [this](bool checked) {
        // Accessibility invokes toggle directly. Restore committed state until the write succeeds.
        {
            const QSignalBlocker blocker(m_favorite);
            m_favorite->setChecked(!checked);
        }
        emit favoriteRequested();
    });
    auto* previous = make(Icon::Previous, QStringLiteral("上一首"), 15);
    m_play = make(Icon::Play, QStringLiteral("播放"), 18);
    m_play->setFixedSize(38, 38);
    m_play->setStyleSheet(QStringLiteral("QToolButton { background: #F5F5F7; border: none; border-radius: 19px; } QToolButton:hover { background: white; } QToolButton:pressed { background: #CCCCD0; }"));
    auto* next = make(Icon::Next, QStringLiteral("下一首"), 15);
    auto* expand = make(Icon::Expand, QStringLiteral("展开播放器"), 13);
    layout->addWidget(m_art);
    layout->addLayout(meta, 1);
    auto* controls = new QHBoxLayout();
    controls->setSpacing(2);
    controls->addWidget(previous);
    controls->addWidget(m_play);
    controls->addWidget(next);
    layout->addWidget(m_favorite);
    layout->addLayout(controls);
    layout->addWidget(expand);
    outer->addLayout(layout);
    m_seek = new MiniSeekSlider(this);
    m_seek->setObjectName(QStringLiteral("miniPlaybackSeek"));
    m_seek->setAccessibleName(QStringLiteral("播放进度"));
    m_seek->setFixedHeight(14);
    m_seek->setRange(0, 0);
    m_seek->setSingleStep(5000);
    m_seek->setPageStep(10000);
    m_seek->setEnabled(false);
    m_seek->setCursor(Qt::PointingHandCursor);
    m_seek->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 3px; border: none; border-radius: 1px; background: #48484D; }"
        "QSlider::sub-page:horizontal { background: #FA2D48; border-radius: 1px; }"
        "QSlider::handle:horizontal { background: #F5F5F7; width: 8px; margin: -3px 0; border-radius: 4px; }"
        "QSlider::handle:horizontal:hover { background: white; }"
        "QSlider::handle:horizontal:disabled { background: transparent; }"
        "QSlider::sub-page:horizontal:disabled { background: #48484D; }"));
    outer->addWidget(m_seek);
    connect(m_seek, &QSlider::sliderReleased, this, [this] { emit seekRequested(m_seek->value()); });
    connect(m_seek, &QSlider::valueChanged, this, [this](int value) {
        if (!m_seek->isSliderDown())
            emit seekRequested(value);
    });
    connect(previous, &QToolButton::clicked, this, &MiniPlayer::previousRequested);
    connect(m_play, &QToolButton::clicked, this, &MiniPlayer::toggleRequested);
    connect(next, &QToolButton::clicked, this, &MiniPlayer::nextRequested);
    connect(expand, &QToolButton::clicked, this, &MiniPlayer::expandRequested);
    setTrack({}, {}, false);
    applyWindowChrome(this);
}

void MiniPlayer::setFavorite(bool liked, bool enabled)
{
    setFavoriteButton(m_favorite, liked, enabled);
}

void MiniPlayer::setTrack(const Track& track, const QPixmap& artwork, bool playing)
{
    if (m_initialized && track == m_lastTrack && artwork.cacheKey() == m_artworkKey
        && devicePixelRatioF() == m_artworkDpr && playing == m_lastPlaying) return;
    m_initialized = true;
    m_lastTrack = track;
    m_artworkKey = artwork.cacheKey();
    m_lastArtwork = artwork;
    m_artworkDpr = devicePixelRatioF();
    m_lastPlaying = playing;
    const bool canSeek = track.id > 0 && !track.missing && track.durationMs > 0;
    if (m_trackId != track.id || !canSeek) {
        const QSignalBlocker blocker(m_seek);
        m_seek->setSliderDown(false);
        m_seek->setRange(0, static_cast<int>(std::clamp<qint64>(track.durationMs, 0, std::numeric_limits<int>::max())));
        m_seek->setValue(0);
    }
    m_trackId = track.id;
    m_canSeek = canSeek;
    m_seek->setEnabled(canSeek);
    m_favorite->setEnabled(track.id > 0);
    const QString title = track.id ? track.displayTitle() : QStringLiteral("未在播放");
    m_title->setText(m_title->fontMetrics().elidedText(title, Qt::ElideRight, m_title->width()));
    m_title->setToolTip(title);
    m_artist->setText(m_artist->fontMetrics().elidedText(track.id ? track.displayArtist() : QString(), Qt::ElideRight, m_artist->width()));
    m_artist->setToolTip(track.displayArtist());
    m_play->setIcon(icon(playing ? Icon::Pause : Icon::Play, QColor(QStringLiteral("#1C1C1E")), 18));
    m_play->setToolTip(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    m_play->setAccessibleName(m_play->toolTip());
    const QPixmap source = artwork.isNull() ? placeholderCover(60) : artwork;
    const qreal scale = devicePixelRatioF();
    QPixmap rounded(qRound(60 * scale), qRound(60 * scale));
    rounded.setDevicePixelRatio(scale);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, 60, 60), 8, 8);
    painter.setClipPath(clip);
    painter.drawPixmap(QRect(0, 0, 60, 60), source);
    painter.end();
    m_art->setPixmap(rounded);
}

void MiniPlayer::setPosition(qint64 positionMs, qint64 durationMs, bool seekable)
{
    if (m_seek->isSliderDown())
        return;
    const QSignalBlocker blocker(m_seek);
    const auto duration = std::clamp<qint64>(durationMs, 0, std::numeric_limits<int>::max());
    m_seek->setRange(0, static_cast<int>(duration));
    m_seek->setValue(static_cast<int>(std::clamp<qint64>(positionMs, 0, duration)));
    m_seek->setEnabled(m_canSeek && duration > 0 && seekable);
    m_seek->setToolTip(formatDuration(positionMs) + QStringLiteral(" / ") + formatDuration(durationMs));
}

bool MiniPlayer::event(QEvent* event)
{
    const bool result = QWidget::event(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange && m_initialized) {
        m_initialized = false;
        setTrack(m_lastTrack, m_lastArtwork, m_lastPlaying);
    }
#endif
    return result;
}

void MiniPlayer::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(255, 255, 255, 35));
    painter.setBrush(QColor(QStringLiteral("#242426")));
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 14, 14);
}

void MiniPlayer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_dragging = true;
#ifndef Q_OS_MACOS
        if (windowHandle() && windowHandle()->startSystemMove()) m_dragging = false;
#endif
        event->accept();
    }
}

void MiniPlayer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && event->buttons().testFlag(Qt::LeftButton)) move(event->globalPosition().toPoint() - m_dragOffset);
}

void MiniPlayer::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void MiniPlayer::closeEvent(QCloseEvent* event)
{
    hide();
    event->ignore();
}

} // namespace echo
