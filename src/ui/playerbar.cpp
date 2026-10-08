#include "ui/playerbar.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace echo {

PlayerBar::PlayerBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(84);
    const Palette colors = echo::palette();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 8);
    m_art = new QLabel(this);
    m_art->setFixedSize(56, 56);
    m_art->setStyleSheet(QStringLiteral("background: %1; border-radius: 6px;").arg(colors.sidebar.name()));
    m_art->setCursor(Qt::PointingHandCursor);
    auto* meta = new QVBoxLayout();
    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_artist = new QLabel(this);
    m_artist->setStyleSheet(QStringLiteral("color: %1;").arg(colors.secondary.name()));
    m_format = new QLabel(this);
    m_format->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(colors.secondary.name()));
    meta->addStretch();
    meta->addWidget(m_title);
    meta->addWidget(m_artist);
    meta->addWidget(m_format);
    meta->addStretch();

    auto* transport = new QVBoxLayout();
    auto* buttons = new QHBoxLayout();
    auto make = [&](Icon kind) {
        auto* button = new QToolButton(this);
        button->setIcon(icon(kind, colors.text, 18));
        button->setAutoRaise(true);
        button->setIconSize(QSize(18, 18));
        return button;
    };
    m_shuffle = make(Icon::Shuffle);
    auto* previous = make(Icon::Previous);
    m_play = make(Icon::Play);
    m_play->setIconSize(QSize(22, 22));
    auto* next = make(Icon::Next);
    m_repeat = make(Icon::Repeat);
    buttons->addStretch();
    buttons->addWidget(m_shuffle);
    buttons->addWidget(previous);
    buttons->addWidget(m_play);
    buttons->addWidget(next);
    buttons->addWidget(m_repeat);
    buttons->addStretch();
    auto* seekRow = new QHBoxLayout();
    m_elapsed = new QLabel(QStringLiteral("0:00"), this);
    m_duration = new QLabel(QStringLiteral("0:00"), this);
    m_seek = new QSlider(Qt::Horizontal, this);
    m_seek->setRange(0, 1000);
    seekRow->addWidget(m_elapsed);
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_duration);
    transport->addLayout(buttons);
    transport->addLayout(seekRow);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setValue(100);
    m_volume->setFixedWidth(110);
    auto* queue = make(Icon::Queue);
    auto* lyrics = make(Icon::Lyrics);
    layout->addWidget(m_art);
    layout->addLayout(meta);
    layout->addStretch();
    layout->addLayout(transport, 1);
    layout->addStretch();
    layout->addWidget(new QLabel(this));
    layout->addWidget(m_volume);
    layout->addWidget(lyrics);
    layout->addWidget(queue);

    connect(m_play, &QToolButton::clicked, this, &PlayerBar::toggleRequested);
    connect(previous, &QToolButton::clicked, this, &PlayerBar::previousRequested);
    connect(next, &QToolButton::clicked, this, &PlayerBar::nextRequested);
    connect(m_shuffle, &QToolButton::clicked, this, &PlayerBar::shuffleToggled);
    connect(m_repeat, &QToolButton::clicked, this, &PlayerBar::repeatToggled);
    connect(queue, &QToolButton::clicked, this, &PlayerBar::queueToggled);
    connect(lyrics, &QToolButton::clicked, this, &PlayerBar::lyricsRequested);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) { emit volumeChanged(value / 100.f); });
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        emit seekRequested(m_seek->value());
    });
    m_art->installEventFilter(this);
}

void PlayerBar::setTrack(const Track& track, const QPixmap& artwork)
{
    if (track.id == 0) {
        m_title->setText(QStringLiteral("未在播放"));
        m_artist->clear();
        m_art->setPixmap({});
        return;
    }
    m_title->setText(track.missing ? track.displayTitle() + QStringLiteral("（找不到文件）") : track.displayTitle());
    m_artist->setText(track.displayArtist());
    if (!artwork.isNull()) {
        m_art->setPixmap(artwork.scaled(56 * artwork.devicePixelRatio(), 56 * artwork.devicePixelRatio(), Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation));
    } else {
        m_art->setPixmap({});
    }
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
    m_play->setIcon(icon(playing ? Icon::Pause : Icon::Play, echo::palette().text, 22));
}

void PlayerBar::setVolume(float volume)
{
    m_volume->blockSignals(true);
    m_volume->setValue(static_cast<int>(volume * 100));
    m_volume->blockSignals(false);
}

void PlayerBar::setShuffle(bool enabled)
{
    m_shuffle->setIcon(icon(Icon::Shuffle, enabled ? echo::palette().accent : echo::palette().text, 18));
}

void PlayerBar::setRepeat(int mode)
{
    m_repeat->setIcon(icon(Icon::Repeat, mode == 0 ? echo::palette().text : echo::palette().accent, 18));
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

bool PlayerBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_art && event->type() == QEvent::MouseButtonRelease) {
        emit artworkClicked();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace echo
