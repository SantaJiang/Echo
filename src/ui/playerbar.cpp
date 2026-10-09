#include "ui/playerbar.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QPaintEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace echo {
namespace {

class BarSlider : public QSlider {
public:
    QColor fill {QStringLiteral("#1D1D1F")};

    explicit BarSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
        setFixedHeight(16);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const qreal pad = 7;
        const qreal y = height() / 2.0;
        const QRectF track(pad, y - 1.5, width() - pad * 2, 3);
        const bool dark = echo::palette().dark;
        painter.setBrush(dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 32));
        painter.drawRoundedRect(track, 1.5, 1.5);
        const double span = maximum() - minimum();
        const double t = span <= 0 ? 0 : (value() - minimum()) / span;
        if (t > 0.001) {
            QRectF played = track;
            played.setWidth(std::max(3.0, track.width() * t));
            painter.setBrush(fill);
            painter.drawRoundedRect(played, 1.5, 1.5);
        }
        painter.setBrush(t > 0.001 ? fill : echo::palette().secondary);
        painter.drawEllipse(QPointF(pad + track.width() * t, y), 4.5, 4.5);
    }
};

QPixmap roundedCover(const QPixmap& source, int side)
{
    if (source.isNull()) {
        return placeholderCover(side);
    }
    QPixmap output(side * 2, side * 2);
    output.setDevicePixelRatio(2);
    output.fill(Qt::transparent);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, side, side), 6, 6);
    painter.setClipPath(path);
    painter.drawPixmap(QRect(0, 0, side, side), source);
    return output;
}

} // namespace

PlayerBar::PlayerBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playerBar"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(84);
    const Palette colors = echo::palette();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignVCenter);

    m_art = new QLabel(this);
    m_art->setFixedSize(48, 48);
    m_art->setPixmap(placeholderCover(48));
    auto* meta = new QVBoxLayout();
    meta->setSpacing(1);
    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    m_artist = new QLabel(this);
    m_artist->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(colors.secondary.name()));
    m_format = new QLabel(this);
    m_format->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(colors.secondary.name()));
    meta->addStretch();
    meta->addWidget(m_title);
    meta->addWidget(m_artist);
    meta->addWidget(m_format);
    meta->addStretch();
    auto* left = new QHBoxLayout();
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(12);
    left->setAlignment(Qt::AlignVCenter);
    left->addWidget(m_art, 0, Qt::AlignVCenter);
    left->addLayout(meta);
    auto* leftWrap = new QWidget(this);
    leftWrap->setLayout(left);
    leftWrap->setFixedWidth(280);
    leftWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto make = [&](Icon kind, int size) {
        auto* button = new QToolButton(this);
        button->setIcon(icon(kind, colors.text, size));
        button->setAutoRaise(true);
        button->setIconSize(QSize(size, size));
        button->setFixedSize(36, 36);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(2);
    m_shuffle = make(Icon::Shuffle, 16);
    auto* previous = make(Icon::Previous, 16);
    m_play = make(Icon::Play, 18);
    auto* next = make(Icon::Next, 16);
    m_repeat = make(Icon::Repeat, 16);
    m_shuffle->setToolTip(QStringLiteral("随机播放"));
    previous->setToolTip(QStringLiteral("上一首"));
    m_play->setToolTip(QStringLiteral("播放"));
    next->setToolTip(QStringLiteral("下一首"));
    m_repeat->setToolTip(QStringLiteral("循环播放"));
    buttons->addStretch();
    buttons->addWidget(m_shuffle, 0, Qt::AlignVCenter);
    buttons->addWidget(previous, 0, Qt::AlignVCenter);
    buttons->addWidget(m_play, 0, Qt::AlignVCenter);
    buttons->addWidget(next, 0, Qt::AlignVCenter);
    buttons->addWidget(m_repeat, 0, Qt::AlignVCenter);
    buttons->addStretch();

    m_elapsed = new QLabel(QStringLiteral("0:00"), this);
    m_duration = new QLabel(QStringLiteral("0:00"), this);
    const QString timeStyle = QStringLiteral("color: %1; font-size: 11px; padding: 0; margin: 0;").arg(colors.secondary.name());
    m_elapsed->setStyleSheet(timeStyle);
    m_duration->setStyleSheet(timeStyle);
    m_elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_duration->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_elapsed->setFixedSize(44, 16);
    m_duration->setFixedSize(48, 16);
    auto* seek = new BarSlider(this);
    seek->fill = colors.accent;
    seek->setRange(0, 1000);
    m_seek = seek;
    auto* seekRow = new QHBoxLayout();
    seekRow->setSpacing(8);
    seekRow->setAlignment(Qt::AlignVCenter);
    seekRow->addWidget(m_elapsed);
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_duration);
    auto* transport = new QVBoxLayout();
    transport->setSpacing(4);
    transport->setContentsMargins(0, 0, 0, 0);
    transport->addLayout(buttons);
    transport->addLayout(seekRow);
    auto* centerWrap = new QWidget(this);
    centerWrap->setLayout(transport);
    centerWrap->setMinimumWidth(360);
    centerWrap->setMaximumWidth(560);
    centerWrap->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* volume = new BarSlider(this);
    volume->fill = colors.text;
    volume->setRange(0, 100);
    volume->setValue(100);
    volume->setFixedWidth(92);
    m_volume = volume;
    auto* volumeIcon = new QLabel(this);
    volumeIcon->setPixmap(icon(Icon::Volume, colors.text, 15).pixmap(15, 15));
    volumeIcon->setFixedSize(18, 18);
    volumeIcon->setAlignment(Qt::AlignCenter);
    auto* queue = make(Icon::Queue, 16);
    queue->setToolTip(QStringLiteral("待播清单"));
    auto* right = new QHBoxLayout();
    right->setSpacing(4);
    right->setAlignment(Qt::AlignVCenter);
    right->addStretch();
    right->addWidget(volumeIcon);
    right->addWidget(m_volume);
    right->addWidget(queue);
    auto* rightWrap = new QWidget(this);
    rightWrap->setLayout(right);
    rightWrap->setFixedWidth(240);
    rightWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    layout->addWidget(leftWrap, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    layout->addWidget(centerWrap, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    layout->addWidget(rightWrap, 0, Qt::AlignVCenter);

    connect(m_play, &QToolButton::clicked, this, &PlayerBar::toggleRequested);
    connect(previous, &QToolButton::clicked, this, &PlayerBar::previousRequested);
    connect(next, &QToolButton::clicked, this, &PlayerBar::nextRequested);
    connect(m_shuffle, &QToolButton::clicked, this, &PlayerBar::shuffleToggled);
    connect(m_repeat, &QToolButton::clicked, this, &PlayerBar::repeatToggled);
    connect(queue, &QToolButton::clicked, this, &PlayerBar::queueToggled);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) { emit volumeChanged(value / 100.f); });
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        emit seekRequested(m_seek->value());
    });
}

void PlayerBar::setTrack(const Track& track, const QPixmap& artwork)
{
    if (track.id == 0) {
        m_title->setText(QStringLiteral("未在播放"));
        m_title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: %1;").arg(echo::palette().secondary.name()));
        m_artist->hide();
        m_format->hide();
        m_art->setPixmap(placeholderCover(48));
        return;
    }
    m_title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    m_title->setText(track.missing ? track.displayTitle() + QStringLiteral("（找不到文件）") : track.displayTitle());
    m_artist->setText(track.displayArtist());
    m_artist->setVisible(!track.displayArtist().isEmpty());
    m_format->setVisible(!m_format->text().isEmpty());
    m_art->setPixmap(roundedCover(artwork, 48));
}

void PlayerBar::setPosition(qint64 positionMs, qint64 durationMs)
{
    m_elapsed->setText(formatDuration(positionMs));
    m_duration->setText(formatDuration(durationMs));
    if (!m_seeking) {
        m_seek->setRange(0, static_cast<int>(std::max<qint64>(durationMs, 1)));
        m_seek->setValue(static_cast<int>(positionMs));
    }
}

void PlayerBar::setPlaying(bool playing)
{
    m_play->setIcon(icon(playing ? Icon::Pause : Icon::Play, echo::palette().text, 18));
    m_play->setIconSize(QSize(18, 18));
}

void PlayerBar::setVolume(float volume)
{
    m_volume->blockSignals(true);
    m_volume->setValue(static_cast<int>(volume * 100));
    m_volume->blockSignals(false);
}

void PlayerBar::setShuffle(bool enabled)
{
    m_shuffle->setIcon(icon(Icon::Shuffle, enabled ? echo::palette().accent : echo::palette().text, 16));
    m_shuffle->setIconSize(QSize(16, 16));
}

void PlayerBar::setRepeat(int mode)
{
    m_repeat->setIcon(icon(Icon::Repeat, mode == 0 ? echo::palette().text : echo::palette().accent, 16));
    m_repeat->setIconSize(QSize(16, 16));
}

void PlayerBar::setFormatText(const QString& text)
{
    m_format->setText(text);
}

void PlayerBar::setStatus(const QString& text)
{
    if (!text.isEmpty()) {
        m_format->setText(text);
    }
}

void PlayerBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const bool dark = echo::palette().dark;
    painter.fillRect(rect(), dark ? QColor(QStringLiteral("#2C2C2E")) : QColor(QStringLiteral("#F2F2F6")));
    painter.setPen(dark ? QColor(255, 255, 255, 40) : QColor(QStringLiteral("#8E8E93")));
    painter.drawLine(0, 0, width(), 0);
}

} // namespace echo
