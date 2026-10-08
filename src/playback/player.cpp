#include "playback/player.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace echo {

Player::Player(Library* library, AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_library(library)
    , m_engine(engine)
{
    connect(m_engine, &AudioEngine::trackStarted, this, [this](qint64 token) {
        for (int index = 0; index < m_queue.size(); ++index) {
            if (m_queue[index].token == token) {
                m_index = index;
                break;
            }
        }
        queueUpcoming();
        emit changed();
    });
    connect(m_engine, &AudioEngine::finished, this, [this] {
        if (m_skipAfterError) {
            m_skipAfterError = false;
            if (m_index + 1 < m_queue.size()) {
                playIndex(m_index + 1, false, 0);
            } else {
                emit changed();
            }
            return;
        }
        if (m_repeat == Repeat::One && m_index >= 0) {
            playIndex(m_index, false, 0);
            return;
        }
        if (m_repeat == Repeat::All && !m_queue.isEmpty()) {
            playIndex((m_index + 1) % m_queue.size(), false, 0);
            return;
        }
        emit changed();
    });
    connect(m_engine, &AudioEngine::stateChanged, this, [this] { emit changed(); });
    connect(m_engine, &AudioEngine::errorOccurred, this, [this] {
        if (m_engine->state() != AudioEngine::State::Playing) {
            m_skipAfterError = true;
        }
        emit changed();
    });
}

void Player::playTracks(const QVector<qint64>& ids, int startIndex, bool albumContext)
{
    m_queue.clear();
    m_history.clear();
    for (const qint64 id : ids) {
        if (id <= 0) {
            continue;
        }
        Entry entry;
        entry.token = m_nextToken++;
        entry.trackId = id;
        entry.albumContext = albumContext;
        m_queue.push_back(entry);
    }
    if (m_queue.isEmpty()) {
        emit changed();
        return;
    }
    if (m_shuffle && m_queue.size() > 1) {
        const Entry start = m_queue[std::clamp(startIndex, 0, static_cast<int>(m_queue.size() - 1))];
        QVector<Entry> rest;
        for (const Entry& entry : m_queue) {
            if (entry.token != start.token) {
                rest.push_back(entry);
            }
        }
        for (int index = rest.size() - 1; index > 0; --index) {
            const int swap = QRandomGenerator::global()->bounded(index + 1);
            std::swap(rest[index], rest[swap]);
        }
        m_queue = {start};
        m_queue += rest;
        startIndex = 0;
    }
    playIndex(std::clamp(startIndex, 0, static_cast<int>(m_queue.size() - 1)), false, 0);
}

void Player::toggle()
{
    if (m_engine->state() == AudioEngine::State::Playing) {
        m_engine->pause();
    } else if (m_engine->state() == AudioEngine::State::Paused) {
        m_engine->resume();
    } else if (m_index >= 0) {
        playIndex(m_index, true, m_engine->positionMs());
    }
    emit changed();
}

void Player::next()
{
    if (m_queue.isEmpty()) {
        return;
    }
    if (m_repeat == Repeat::One) {
        playIndex(m_index, false, 0);
        return;
    }
    int nextIndex = m_index + 1;
    if (nextIndex >= m_queue.size()) {
        if (m_repeat != Repeat::All) {
            m_engine->stop();
            emit changed();
            return;
        }
        nextIndex = 0;
    }
    rememberHistory();
    playIndex(nextIndex, false, 0);
}

void Player::previous()
{
    if (m_engine->positionMs() > 3000) {
        m_engine->seekMs(0);
        emit changed();
        return;
    }
    if (!m_history.isEmpty()) {
        const qint64 token = m_history.takeLast();
        for (int index = 0; index < m_queue.size(); ++index) {
            if (m_queue[index].token == token) {
                playIndex(index, false, 0);
                return;
            }
        }
    }
    if (m_index > 0) {
        playIndex(m_index - 1, false, 0);
    } else {
        m_engine->seekMs(0);
        emit changed();
    }
}

void Player::seek(qint64 positionMs)
{
    m_engine->seekMs(std::max<qint64>(0, positionMs));
}

void Player::setVolume(float slider)
{
    m_volume = std::clamp(slider, 0.f, 1.f);
    m_engine->setVolume(m_volume);
    emit changed();
}

void Player::setExclusive(bool exclusive)
{
    m_exclusive = exclusive;
    m_engine->setExclusive(exclusive);
    emit changed();
}

void Player::setReplayGainEnabled(bool enabled)
{
    m_replayGain = enabled;
    if (m_index >= 0 && m_index < m_queue.size()) {
        playIndex(m_index, true, m_engine->positionMs());
    }
    emit changed();
}

void Player::setShuffle(bool shuffle)
{
    m_shuffle = shuffle;
    emit changed();
}

void Player::setRepeat(Repeat repeat)
{
    m_repeat = repeat;
    queueUpcoming();
    emit changed();
}

void Player::enqueueNext(const QVector<qint64>& ids)
{
    int insertAt = m_index + 1;
    for (const qint64 id : ids) {
        Entry entry;
        entry.token = m_nextToken++;
        entry.trackId = id;
        m_queue.insert(std::clamp(insertAt, 0, static_cast<int>(m_queue.size())), entry);
        ++insertAt;
    }
    queueUpcoming();
    emit changed();
}

void Player::enqueue(const QVector<qint64>& ids)
{
    for (const qint64 id : ids) {
        Entry entry;
        entry.token = m_nextToken++;
        entry.trackId = id;
        m_queue.push_back(entry);
    }
    if (m_engine->state() == AudioEngine::State::Stopped && m_index < 0 && !m_queue.isEmpty()) {
        playIndex(0, false, 0);
        return;
    }
    queueUpcoming();
    emit changed();
}

void Player::moveQueue(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_queue.size() || to >= m_queue.size() || from == to) {
        return;
    }
    const qint64 currentToken = m_index >= 0 ? m_queue[m_index].token : 0;
    const Entry entry = m_queue.takeAt(from);
    m_queue.insert(to, entry);
    for (int index = 0; index < m_queue.size(); ++index) {
        if (m_queue[index].token == currentToken) {
            m_index = index;
        }
    }
    queueUpcoming();
    emit changed();
}

void Player::removeQueueRow(int row)
{
    if (row < 0 || row >= m_queue.size()) {
        return;
    }
    const bool current = row == m_index;
    m_queue.removeAt(row);
    if (m_queue.isEmpty()) {
        m_index = -1;
        m_engine->stop();
        emit changed();
        return;
    }
    if (current) {
        playIndex(std::min(row, static_cast<int>(m_queue.size() - 1)), false, 0);
        return;
    }
    if (row < m_index) {
        --m_index;
    }
    queueUpcoming();
    emit changed();
}

void Player::clearUpcoming()
{
    if (m_index < 0) {
        m_queue.clear();
    } else if (m_index + 1 < m_queue.size()) {
        m_queue.resize(m_index + 1);
    }
    m_engine->setUpcoming({});
    emit changed();
}

void Player::restoreSession()
{
    m_volume = m_library->setting(QStringLiteral("volume"), QStringLiteral("1")).toFloat();
    m_exclusive = m_library->setting(QStringLiteral("exclusive"), QStringLiteral("0")) == QLatin1String("1");
    m_replayGain = m_library->setting(QStringLiteral("replaygain"), QStringLiteral("1")) != QLatin1String("0");
    m_shuffle = m_library->setting(QStringLiteral("shuffle"), QStringLiteral("0")) == QLatin1String("1");
    m_repeat = static_cast<Repeat>(m_library->setting(QStringLiteral("repeat"), QStringLiteral("0")).toInt());
    m_engine->setVolume(m_volume);
    const auto document = QJsonDocument::fromJson(m_library->setting(QStringLiteral("queue")).toUtf8());
    QVector<qint64> ids;
    for (const QJsonValue& value : document.array()) {
        ids.push_back(value.toInteger());
    }
    const int index = m_library->setting(QStringLiteral("queue_index"), QStringLiteral("-1")).toInt();
    const qint64 position = m_library->setting(QStringLiteral("position_ms"), QStringLiteral("0")).toLongLong();
    if (!ids.isEmpty()) {
        m_queue.clear();
        for (const qint64 id : ids) {
            Entry entry;
            entry.token = m_nextToken++;
            entry.trackId = id;
            m_queue.push_back(entry);
        }
        m_index = std::clamp(index, 0, static_cast<int>(m_queue.size() - 1));
        Q_UNUSED(position);
    }
    emit changed();
}

void Player::saveSession() const
{
    QJsonArray ids;
    for (const Entry& entry : m_queue) {
        ids.append(entry.trackId);
    }
    m_library->setSetting(QStringLiteral("queue"), QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));
    m_library->setSetting(QStringLiteral("queue_index"), QString::number(m_index));
    m_library->setSetting(QStringLiteral("position_ms"), QString::number(m_engine->positionMs()));
    m_library->setSetting(QStringLiteral("volume"), QString::number(m_volume, 'f', 3));
    m_library->setSetting(QStringLiteral("exclusive"), m_exclusive ? QStringLiteral("1") : QStringLiteral("0"));
    m_library->setSetting(QStringLiteral("replaygain"), m_replayGain ? QStringLiteral("1") : QStringLiteral("0"));
    m_library->setSetting(QStringLiteral("shuffle"), m_shuffle ? QStringLiteral("1") : QStringLiteral("0"));
    m_library->setSetting(QStringLiteral("repeat"), QString::number(static_cast<int>(m_repeat)));
}

Track Player::currentTrack() const
{
    if (m_index < 0 || m_index >= m_queue.size()) {
        return {};
    }
    const qint64 active = m_engine->activeToken();
    for (const Entry& entry : m_queue) {
        if (entry.token == active) {
            return m_library->track(entry.trackId);
        }
    }
    return m_library->track(m_queue[m_index].trackId);
}

qint64 Player::positionMs() const
{
    const Track item = currentTrack();
    if (item.cueStartMs >= 0) {
        return std::max<qint64>(0, m_engine->positionMs() - item.cueStartMs);
    }
    return m_engine->positionMs();
}

AudioEngine::State Player::state() const
{
    return m_engine->state();
}

void Player::playIndex(int index, bool resumePosition, qint64 positionMs)
{
    if (index < 0 || index >= m_queue.size()) {
        return;
    }
    m_index = index;
    PlaybackItem current = itemFor(m_queue[index]);
    if (resumePosition) {
        current.startMs += positionMs;
    }
    PlaybackItem upcoming;
    if (index + 1 < m_queue.size()) {
        upcoming = itemFor(m_queue[index + 1]);
    } else if (m_repeat == Repeat::All && m_queue.size() > 1) {
        upcoming = itemFor(m_queue[0]);
    } else if (m_repeat == Repeat::One) {
        upcoming = itemFor(m_queue[index]);
        upcoming.token = m_nextToken++;
    }
    m_engine->setVolume(m_volume);
    m_engine->play(current, upcoming);
    emit changed();
}

void Player::queueUpcoming()
{
    if (m_index < 0 || m_index >= m_queue.size()) {
        return;
    }
    if (m_index + 1 < m_queue.size()) {
        m_engine->setUpcoming(itemFor(m_queue[m_index + 1]));
    } else if (m_repeat == Repeat::All && !m_queue.isEmpty()) {
        m_engine->setUpcoming(itemFor(m_queue[0]));
    } else if (m_repeat == Repeat::One) {
        PlaybackItem again = itemFor(m_queue[m_index]);
        again.token = m_nextToken++;
        m_engine->setUpcoming(again);
    } else {
        m_engine->setUpcoming({});
    }
}

PlaybackItem Player::itemFor(const Entry& entry) const
{
    const Track track = m_library->track(entry.trackId);
    PlaybackItem item;
    item.token = entry.token;
    item.trackId = entry.trackId;
    item.path = track.path;
    item.startMs = std::max<qint64>(0, track.cueStartMs);
    item.endMs = track.cueEndMs;
    item.gainDb = gainFor(track, entry.albumContext);
    return item;
}

float Player::gainFor(const Track& track, bool albumContext) const
{
    if (!m_replayGain) {
        return 0;
    }
    const bool useAlbum = albumContext && track.hasAlbumGain;
    if (!useAlbum && !track.hasTrackGain && !track.hasAlbumGain) {
        return 0;
    }
    double db = useAlbum ? track.albumGainDb : (track.hasTrackGain ? track.trackGainDb : track.albumGainDb);
    const double peak = useAlbum ? track.albumPeak : (track.hasTrackGain ? track.trackPeak : track.albumPeak);
    double linear = std::pow(10.0, db / 20.0);
    if (peak > 0.01 && linear * peak > 1.0) {
        linear = 1.0 / peak;
        db = 20.0 * std::log10(linear);
    }
    return static_cast<float>(db);
}

void Player::rememberHistory()
{
    if (m_index >= 0 && m_index < m_queue.size()) {
        m_history.push_back(m_queue[m_index].token);
        if (m_history.size() > 200) {
            m_history.removeFirst();
        }
    }
}

} // namespace echo
