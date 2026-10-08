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
    m_art->setPixmap(placeholderCover(64));
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
    m_play->setIcon(icon(Icon::Play, echo::palette().text, 16));
    next->setIcon(icon(Icon::Next, echo::palette().text, 16));
    expand->setIcon(icon(Icon::Expand, echo::palette().text, 14));
    expand->setText(QString());
    expand->setToolTip(QStringLiteral("展开"));
    previous->setToolTip(QStringLiteral("上一首"));
    m_play->setToolTip(QStringLiteral("播放"));
    next->setToolTip(QStringLiteral("下一首"));
    for (auto* button : {previous, m_play, next, expand}) {
        button->setAutoRaise(true);
        button->setFixedSize(32, 32);
        button->setIconSize(QSize(16, 16));
    }
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
