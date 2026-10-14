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
#include <QWindow>

namespace echo {

MiniPlayer::MiniPlayer(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("Echo 迷你播放器"));
    setObjectName(QStringLiteral("miniPlayer"));
    setProperty("echoMiniPlayer", true);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(440, 88);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 12, 12, 12);
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
    layout->addLayout(controls);
    layout->addWidget(expand);
    connect(previous, &QToolButton::clicked, this, &MiniPlayer::previousRequested);
    connect(m_play, &QToolButton::clicked, this, &MiniPlayer::toggleRequested);
    connect(next, &QToolButton::clicked, this, &MiniPlayer::nextRequested);
    connect(expand, &QToolButton::clicked, this, &MiniPlayer::expandRequested);
    setTrack({}, {}, false);
    applyWindowChrome(this);
}

void MiniPlayer::setTrack(const Track& track, const QPixmap& artwork, bool playing)
{
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
