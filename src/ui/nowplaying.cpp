#include "ui/nowplaying.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace echo {

QVector<LyricLine> parseLyrics(const QString& text)
{
    QVector<LyricLine> lines;
    const QRegularExpression stamp(QStringLiteral(R"(\[(\d+):(\d+)(?:[.:](\d+))?\])"));
    for (const QString& raw : text.split(u'\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        qint64 time = -1;
        auto match = stamp.match(line);
        if (match.hasMatch()) {
            const int minutes = match.captured(1).toInt();
            const int seconds = match.captured(2).toInt();
            const QString fraction = match.captured(3);
            int millis = 0;
            if (!fraction.isEmpty()) {
                millis = fraction.leftJustified(3, u'0').left(3).toInt();
            }
            time = (minutes * 60 + seconds) * 1000 + millis;
            line.remove(stamp);
            line = line.trimmed();
        }
        if (!line.isEmpty()) {
            lines.push_back(LyricLine {time, line});
        }
    }
    return lines;
}

NowPlayingPage::NowPlayingPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("nowPlaying"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(48, 24, 48, 24);
    auto* left = new QVBoxLayout();
    m_art = new QLabel(this);
    m_art->setFixedSize(280, 280);
    m_art->setAlignment(Qt::AlignCenter);
    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignHCenter);
    m_title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700;"));
    m_title->setWordWrap(true);
    m_artist = new QLabel(this);
    m_artist->setAlignment(Qt::AlignHCenter);
    m_artist->setStyleSheet(QStringLiteral("color: %1; font-size: 18px;").arg(echo::palette().accent.name()));
    left->addStretch();
    left->addWidget(m_art, 0, Qt::AlignHCenter);
    left->addSpacing(16);
    left->addWidget(m_title);
    left->addWidget(m_artist);
    left->addStretch();
    m_lyricsPane = new QWidget(this);
    auto* right = new QVBoxLayout(m_lyricsPane);
    right->setContentsMargins(24, 0, 0, 0);
    auto* edit = new QPushButton(QStringLiteral("编辑歌词"), m_lyricsPane);
    edit->setObjectName(QStringLiteral("secondaryButton"));
    edit->setFlat(true);
    m_lyrics = new QListWidget(m_lyricsPane);
    m_lyrics->setFrameShape(QFrame::NoFrame);
    m_lyrics->setFocusPolicy(Qt::NoFocus);
    right->addWidget(edit, 0, Qt::AlignRight);
    right->addWidget(m_lyrics, 1);
    layout->addStretch(1);
    layout->addLayout(left, 0);
    layout->addWidget(m_lyricsPane, 1);
    layout->addStretch(1);
    m_lyricsPane->hide();
    connect(edit, &QPushButton::clicked, this, &NowPlayingPage::editLyricsRequested);
}

void NowPlayingPage::setTrack(const Track& track, const QPixmap& artwork, qint64 positionMs)
{
    if (auto* edit = findChild<QPushButton*>(QStringLiteral("secondaryButton"))) {
        edit->setVisible(track.id != 0);
    }
    m_title->setText(track.id ? track.displayTitle() : QStringLiteral("未在播放"));
    m_artist->setText(track.id ? track.displayArtist() : QString());
    if (!artwork.isNull()) {
        m_art->setPixmap(artwork.scaled(280, 280, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_art->setPixmap(placeholderCover(280));
    }
    if (m_lyricsPane) {
        m_lyricsPane->setVisible(!track.lyrics.trimmed().isEmpty());
    }
    const auto parsed = parseLyrics(track.lyrics);
    if (parsed.size() != m_lines.size() || (!parsed.isEmpty() && !m_lines.isEmpty() && parsed.first().text != m_lines.first().text)) {
        m_lines = parsed;
        m_lyrics->clear();
        for (const LyricLine& line : m_lines) {
            m_lyrics->addItem(line.text);
        }
    }
    int active = -1;
    for (int index = 0; index < m_lines.size(); ++index) {
        if (m_lines[index].timeMs >= 0 && m_lines[index].timeMs <= positionMs) {
            active = index;
        }
    }
    if (active >= 0) {
        for (int index = 0; index < m_lyrics->count(); ++index) {
            m_lyrics->item(index)->setForeground(index == active ? echo::palette().text : echo::palette().secondary);
        }
        m_lyrics->scrollToItem(m_lyrics->item(active), QAbstractItemView::PositionAtCenter);
    }
}

} // namespace echo
