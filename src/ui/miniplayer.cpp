#include "ui/miniplayer.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace echo {

MiniPlayer::MiniPlayer(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("Echo"));
    setFixedSize(360, 92);
    auto* layout = new QHBoxLayout(this);
    m_art = new QLabel(this);
    m_art->setFixedSize(64, 64);
    auto* meta = new QVBoxLayout();
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_artist = new QLabel(this);
    m_artist->setStyleSheet(QStringLiteral("color: %1;").arg(echo::palette().secondary.name()));
    meta->addWidget(m_title);
    meta->addWidget(m_artist);
    auto* previous = new QToolButton(this);
    m_play = new QToolButton(this);
    auto* next = new QToolButton(this);
    auto* expand = new QToolButton(this);
    previous->setIcon(icon(Icon::Previous, echo::palette().text, 16));
    m_play->setIcon(icon(Icon::Play, echo::palette().text, 18));
    next->setIcon(icon(Icon::Next, echo::palette().text, 16));
    expand->setText(QStringLiteral("展开"));
    previous->setAutoRaise(true);
    m_play->setAutoRaise(true);
    next->setAutoRaise(true);
    expand->setAutoRaise(true);
    layout->addWidget(m_art);
    layout->addLayout(meta, 1);
    layout->addWidget(previous);
    layout->addWidget(m_play);
    layout->addWidget(next);
    layout->addWidget(expand);
    connect(previous, &QToolButton::clicked, this, &MiniPlayer::previousRequested);
    connect(m_play, &QToolButton::clicked, this, &MiniPlayer::toggleRequested);
    connect(next, &QToolButton::clicked, this, &MiniPlayer::nextRequested);
    connect(expand, &QToolButton::clicked, this, &MiniPlayer::expandRequested);
}

void MiniPlayer::setTrack(const Track& track, const QPixmap& artwork, bool playing)
{
    m_title->setText(track.id ? track.displayTitle() : QStringLiteral("未在播放"));
    m_artist->setText(track.displayArtist());
    m_play->setIcon(icon(playing ? Icon::Pause : Icon::Play, echo::palette().text, 18));
    if (!artwork.isNull()) {
        m_art->setPixmap(artwork.scaled(64, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

void MiniPlayer::closeEvent(QCloseEvent* event)
{
    emit expandRequested();
    event->accept();
}

} // namespace echo
