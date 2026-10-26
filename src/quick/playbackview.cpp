#include "quick/playbackview.h"
#include "library/library.h"
#include "playback/player.h"
#include <QRegularExpression>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>

namespace echo {
PlaybackView::PlaybackView(Library* library, Player* player, AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_library(library)
    , m_player(player)
    , m_engine(engine)
{
    connect(player, &Player::changed, this, [this] { refresh(); });
    connect(library, &Library::favoritesChanged, this, &PlaybackView::trackChanged);
    connect(library, &Library::changed, this, [this] { refresh(true); });
    connect(library, &Library::sourceChanged, this, [this](qint64) { refresh(true); });
    connect(engine, &AudioEngine::positionChanged, this, &PlaybackView::positionChanged);
    connect(engine, &AudioEngine::stateChanged, this, [this] {
        if (playing() && m_presentationActive)
            m_clock.start();
        else
            m_clock.stop();
        emit playbackChanged();
        emit positionChanged();
    });
    m_clock.setInterval(100);
    connect(&m_clock, &QTimer::timeout, this, &PlaybackView::positionChanged);
    refresh();
    if (playing())
        m_clock.start();
}

QVariantMap PlaybackView::track() const
{
    return { { "id", m_track.id }, { "title", m_track.id ? m_track.displayTitle() : QStringLiteral("音乐，回到此刻") },
        { "artist", m_track.id ? m_track.displayArtist() : QStringLiteral("选择一首歌曲，开始聆听") },
        { "album", m_track.displayAlbum() },
        { "artwork",
            (m_track.artworkHash.isEmpty() || !QFileInfo::exists(m_library->artwork()->filePath(m_track.artworkHash)))
                ? QString()
                : QUrl::fromLocalFile(m_library->artwork()->filePath(m_track.artworkHash)).toString() },
        { "favorite", m_library->isFavorite(m_track.id) } };
}
bool PlaybackView::playing() const { return m_player->state() == AudioEngine::State::Playing; }
qint64 PlaybackView::position() const { return m_player->positionMs(); }
float PlaybackView::volume() const { return m_player->volume(); }
bool PlaybackView::shuffle() const { return m_player->shuffle(); }
int PlaybackView::repeat() const { return static_cast<int>(m_player->repeat()); }
void PlaybackView::toggle() { m_player->toggle(); }
void PlaybackView::next() { m_player->next(); }
void PlaybackView::previous() { m_player->previous(); }
void PlaybackView::seek(qint64 ms)
{
    if (m_track.id)
        m_player->seek(std::clamp(ms, qint64(0), duration()));
}
void PlaybackView::toggleFavorite()
{
    if (m_track.id)
        m_library->setFavorites({ m_track.id }, !m_library->isFavorite(m_track.id));
}
void PlaybackView::toggleShuffle() { m_player->setShuffle(!shuffle()); }
void PlaybackView::cycleRepeat() { m_player->setRepeat(static_cast<Player::Repeat>((repeat() + 1) % 3)); }
void PlaybackView::setVolume(float value) { m_player->setVolume(std::clamp(value, 0.f, 1.f)); }
void PlaybackView::setPresentationActive(bool active)
{
    m_presentationActive = active;
    if (active && playing())
        m_clock.start();
    else
        m_clock.stop();
    if (active)
        emit positionChanged();
}
void PlaybackView::refresh(bool reloadMetadata)
{
    Track current = m_player->currentTrack();
    if (current.id && reloadMetadata)
        current = m_library->track(current.id);
    if (reloadMetadata || current != m_track) {
        const bool newTrack = current.id != m_track.id;
        const bool newLyrics = newTrack || current.lyrics != m_track.lyrics;
        m_track = current;
        if (newLyrics) {
            m_lyrics.clear();
            // A line may carry multiple LRC timestamps; plain lyrics remain readable.
            const QRegularExpression stamp(QStringLiteral(R"(\[(\d+):(\d{1,2})(?:[.:](\d{1,3}))?\])"));
            const QRegularExpression metadata(QStringLiteral(R"(^\[[a-zA-Z]+:.*\]$)"));
            const QRegularExpression offsetTag(QStringLiteral(R"(\[offset:([+-]?\d+)\])"));
            const auto offsetMatch = offsetTag.match(m_track.lyrics);
            const qint64 offset = offsetMatch.hasMatch() ? offsetMatch.captured(1).toLongLong() : 0;
            for (const QString& line : m_track.lyrics.split(u'\n')) {
                const QString text = QString(line).remove(stamp).trimmed();
                if (text.isEmpty() || metadata.match(text).hasMatch())
                    continue;
                auto matches = stamp.globalMatch(line);
                bool timed = false;
                while (matches.hasNext()) {
                    const auto match = matches.next();
                    const QString fraction = match.captured(3).leftJustified(3, u'0');
                    const qint64 time = match.captured(1).toLongLong() * 60000 + match.captured(2).toInt() * 1000
                        + fraction.toInt() + offset;
                    m_lyrics << QVariantMap { { "text", text }, { "time", time } };
                    timed = true;
                }
                if (!timed)
                    m_lyrics << QVariantMap { { "text", text }, { "time", -1 } };
            }
            std::stable_sort(m_lyrics.begin(), m_lyrics.end(), [](const QVariant& a, const QVariant& b) {
                return a.toMap().value("time").toLongLong() < b.toMap().value("time").toLongLong();
            });
            emit lyricsChanged();
        }
        emit trackChanged();
    }
    emit playbackChanged();
    emit positionChanged();
}
int PlaybackView::lyricIndex() const
{
    const qint64 now = position();
    int result = -1;
    for (int i = 0; i < m_lyrics.size(); ++i) {
        const qint64 time = m_lyrics[i].toMap().value("time").toLongLong();
        if (time >= 0 && time <= now)
            result = i;
        if (time > now)
            break;
    }
    return result;
}
}
