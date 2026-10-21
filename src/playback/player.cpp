#include "playback/player.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QJsonObject>
#include <QtConcurrent>
#include <QTimer>
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
    connect(m_engine, &AudioEngine::spatialStatusChanged, this, [this](const QString& status) {
        m_spatialStatus = status;
        emit spatialChanged();
    });
    connect(this, &Player::changed, this, [this] { scheduleAnalysis(); });
    connect(m_library, &Library::changed, this, [this] {
        if (m_normalization) {
            updateNormalizationGains();
            scheduleAnalysis();
            emit normalizationChanged();
        }
    });
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

Player::~Player()
{
    m_cancelAnalysis.store(true);
    m_analysis.waitForFinished();
}

QString Player::loudnessKey(const Track& track) const
{
    const QFileInfo file(track.path);
    const QString source = QStringLiteral("r128-v1|%1|%2|%3|%4|%5")
        .arg(track.path).arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch())
        .arg(track.cueStartMs).arg(track.cueEndMs);
    return QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
}

LoudnessResult Player::cachedLoudness(const Track& track) const
{
    const QString key = loudnessKey(track);
    if (m_loudness.contains(key)) return m_loudness.value(key);
    const auto json = QJsonDocument::fromJson(m_library->setting(QStringLiteral("loudness/") + key).toUtf8()).object();
    if (!json.contains(QStringLiteral("lufs")) || !json.contains(QStringLiteral("peak"))) return {};
    LoudnessResult result {true, json.value(QStringLiteral("lufs")).toDouble(), json.value(QStringLiteral("peak")).toDouble()};
    m_loudness.insert(key, result);
    return result;
}

void Player::setNormalizationEnabled(bool enabled)
{
    if (m_normalization == enabled) return;
    m_normalization = enabled;
    m_library->setSetting(QStringLiteral("normalization"), enabled ? QStringLiteral("1") : QStringLiteral("0"));
    updateNormalizationGains();
    scheduleAnalysis();
    emit normalizationChanged();
}

void Player::updateNormalizationGains()
{
    for (const auto& entry : m_queue) {
        const Track track = m_library->track(entry.trackId);
        m_engine->setTrackGain(entry.token, gainFor(track, entry.albumContext));
    }
    queueUpcoming();
}

QString Player::normalizationStatus() const
{
    if (!m_normalization) return QStringLiteral("关闭时恢复原有播放音量；分析结果会保留。");
    QSet<QString> seen;
    int done = 0;
    int failed = 0;
    for (const auto& entry : m_queue) {
        const Track track = m_library->track(entry.trackId);
        const QString key = loudnessKey(track);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (cachedLoudness(track).valid) ++done;
        else if (m_analysisFailed.contains(key)) ++failed;
    }
    if (seen.isEmpty()) return QStringLiteral("开始播放后，会自动分析当前播放队列。");
    if (done + failed < seen.size()) return QStringLiteral("正在分析当前队列：%1 / %2；完成后自动应用。").arg(done).arg(seen.size());
    if (failed) return QStringLiteral("已均衡 %1 首；%2 首无法分析，保持原始音量。").arg(done).arg(failed);
    return QStringLiteral("当前队列的 %1 首歌曲已完成音量均衡。").arg(done);
}

void Player::scheduleAnalysis()
{
    if (!m_normalization || m_analysisPending || m_trashing)
        return;
    // Prioritize the audible track, then follow the queue. Duplicate files/ranges share a cache entry.
    QVector<Entry> entries;
    if (m_index >= 0 && m_index < m_queue.size()) entries.push_back(m_queue[m_index]);
    entries += m_queue;
    for (const auto& entry : entries) {
        const Track track = m_library->track(entry.trackId);
        const QString key = loudnessKey(track);
        if (cachedLoudness(track).valid || m_analysisFailed.contains(key)) continue;
        PlaybackItem item;
        item.path = track.path;
        item.startMs = std::max<qint64>(0, track.cueStartMs);
        item.endMs = track.cueEndMs;
        disconnect(&m_analysis, nullptr, this, nullptr);
        connect(&m_analysis, &QFutureWatcher<LoudnessResult>::finished, this, [this, key, track] {
            m_analysisPending = false;
            const auto result = m_analysis.result();
            // Discard a result if the file changed while it was being decoded.
            if (!m_cancelAnalysis.load() && key == loudnessKey(track)) {
                if (result.valid) {
                    m_loudness.insert(key, result);
                    QJsonObject json;
                    json.insert(QStringLiteral("lufs"), result.lufs);
                    json.insert(QStringLiteral("peak"), result.peak);
                    m_library->setSetting(QStringLiteral("loudness/") + key,
                        QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
                    updateNormalizationGains();
                } else {
                    m_analysisFailed.insert(key);
                }
            }
            emit normalizationChanged();
            QTimer::singleShot(0, this, &Player::scheduleAnalysis);
        });
        m_analysisPending = true;
        m_analysis.setFuture(QtConcurrent::run([this, item] {
            return analyzeLoudness(item, [this] { return m_cancelAnalysis.load(); });
        }));
        emit normalizationChanged();
        return;
    }
}

bool Player::replaceContextQueue() const
{
    return m_library->setting(QStringLiteral("replace_context_queue"), QStringLiteral("1")) != QLatin1String("0");
}
void Player::setReplaceContextQueue(bool enabled)
{
    m_library->setSetting(QStringLiteral("replace_context_queue"), enabled ? QStringLiteral("1") : QStringLiteral("0"));
    emit changed();
}
bool Player::playable(qint64 id) const
{
    const auto track = m_library->track(id);
    const QFileInfo info(track.path);
    return track.id && !track.missing && info.isFile() && info.isReadable();
}
int Player::nextPlayable(int from, bool wrap) const
{
    for (int offset = 0; offset < m_queue.size(); ++offset) {
        int at = from + offset;
        if (at >= m_queue.size()) {
            if (!wrap)
                break;
            at %= m_queue.size();
        }
        if (at >= 0 && playable(m_queue[at].trackId))
            return at;
    }
    return -1;
}
void Player::activateQueueRow(int index)
{
    ++m_requestRevision;
    if (index >= 0 && index < m_queue.size()) {
        if (!playable(m_queue[index].trackId)) {
            emit trackUnavailable(m_queue[index].trackId);
            return;
        }
        playIndex(index, false, 0);
    }
}
void Player::playSinglePreservingQueue(qint64 id)
{
    ++m_requestRevision;
    if (!playable(id)) {
        emit trackUnavailable(id);
        return;
    }
    if (m_index >= 0 && m_index < m_queue.size() && m_queue[m_index].trackId == id) {
        playIndex(m_index, false, 0);
        return;
    }
    rememberHistory();
    const int at = std::clamp(m_index + 1, 0, static_cast<int>(m_queue.size()));
    Entry entry;
    entry.trackId = id;
    entry.token = m_nextToken++;
    m_queue.insert(at, entry);
    playIndex(at, false, 0);
}
void Player::playTracks(const QVector<qint64>& ids, int startIndex, bool albumContext, std::optional<bool> shuffle)
{
    const auto revision = ++m_requestRevision;
    const auto tracks = m_library->tracks(ids);
    auto* watcher = new QFutureWatcher<QSet<int>>(this);
    connect(watcher, &QFutureWatcher<QSet<int>>::finished, this,
        [this, watcher, revision, ids, startIndex, albumContext, shuffle]() mutable {
            const auto valid = watcher->result();
            watcher->deleteLater();
            if (revision != m_requestRevision)
                return;
            QVector<Entry> replacement;
            int mappedIndex = 0, skipped = 0;
            for (int i = 0; i < ids.size(); ++i) {
                if (!valid.contains(i)) {
                    ++skipped;
                    continue;
                }
                if (i < startIndex)
                    ++mappedIndex;
                Entry entry;
                entry.token = m_nextToken++;
                entry.trackId = ids[i];
                entry.albumContext = albumContext;
                replacement.push_back(entry);
            }
            if (replacement.isEmpty()) {
                emit playbackMessage(QStringLiteral("没有可播放的歌曲，已保留当前队列。"));
                return;
            }
            if (shuffle)
                m_shuffle = *shuffle;
            m_queue = std::move(replacement);
            m_history.clear();
            startIndex = std::min(mappedIndex, static_cast<int>(m_queue.size() - 1));
            if (skipped)
                emit playbackMessage(QStringLiteral("已跳过 %1 首不可用歌曲").arg(skipped));
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
                m_queue = { start };
                m_queue += rest;
                startIndex = 0;
            }
            playIndex(std::clamp(startIndex, 0, static_cast<int>(m_queue.size() - 1)), false, 0);
        });
    watcher->setFuture(QtConcurrent::run([tracks] {
        QSet<int> valid;
        for (int i = 0; i < tracks.size(); ++i) {
            const auto& track = tracks[i];
            const QFileInfo file(track.path);
            if (track.id && !track.missing && file.isFile() && file.isReadable())
                valid.insert(i);
        }
        return valid;
    }));
}

void Player::toggle()
{
    ++m_requestRevision;
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
    ++m_requestRevision;
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
    ++m_requestRevision;
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
    if (exclusive && m_spatial) {
        m_spatial = false;
        m_library->setSetting(QStringLiteral("spatial_audio"), QStringLiteral("0"));
        emit spatialChanged();
    }
    m_engine->setExclusive(exclusive);
    emit changed();
}

void Player::setSpatialEnabled(bool enabled)
{
#ifdef Q_OS_MACOS
    if (m_spatial == enabled) return;
    m_spatial = enabled;
    if (enabled) m_exclusive = false;
    m_library->setSetting(QStringLiteral("spatial_audio"), enabled ? QStringLiteral("1") : QStringLiteral("0"));
    m_library->setSetting(QStringLiteral("exclusive"), m_exclusive ? QStringLiteral("1") : QStringLiteral("0"));
    m_spatialStatus.clear();
    m_engine->setSpatialEnabled(enabled);
    emit spatialChanged();
    emit changed();
#else
    Q_UNUSED(enabled);
#endif
}

QString Player::spatialStatus() const
{
    if (!m_spatial) return QStringLiteral("空间音频已关闭。");
    return m_spatialStatus.isEmpty() ? QStringLiteral("已启用；开始播放后可在控制中心设置空间音频。") : m_spatialStatus;
}

void Player::setReplayGainEnabled(bool enabled)
{
    m_replayGain = enabled;
    updateNormalizationGains();
    emit changed();
}

void Player::setShuffle(bool shuffle)
{
    m_shuffle = shuffle;
    queueUpcoming();
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
    ++m_requestRevision;
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
    ++m_requestRevision;
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
    ++m_requestRevision;
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
    m_normalization = m_library->setting(QStringLiteral("normalization"), QStringLiteral("0")) == QLatin1String("1");
    m_volume = m_library->setting(QStringLiteral("volume"), QStringLiteral("1")).toFloat();
    m_exclusive = m_library->setting(QStringLiteral("exclusive"), QStringLiteral("0")) == QLatin1String("1");
#ifdef Q_OS_MACOS
    m_spatial = m_library->setting(QStringLiteral("spatial_audio"), QStringLiteral("0")) == QLatin1String("1");
    if (m_spatial) m_exclusive = false;
#endif
    m_engine->setExclusive(m_exclusive);
    m_engine->setSpatialEnabled(m_spatial);
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
    m_library->setSetting(QStringLiteral("spatial_audio"), m_spatial ? QStringLiteral("1") : QStringLiteral("0"));
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
    if (!playable(m_queue[index].trackId)) {
        const int candidate = nextPlayable(index + 1, m_repeat == Repeat::All);
        if (candidate < 0) {
            m_engine->stop();
            emit playbackMessage(QStringLiteral("队列中没有更多可播放歌曲"));
            return;
        }
        index = candidate;
    }
    ++m_playRevision;
    m_index = index;
    PlaybackItem current = itemFor(m_queue[index]);
    if (resumePosition) {
        current.startMs += positionMs;
    }
    PlaybackItem upcoming;
    const int next = m_repeat == Repeat::One ? index : nextPlayable(index + 1, m_repeat == Repeat::All);
    if (next >= 0)
        upcoming = itemFor(m_queue[next]);
    m_engine->setVolume(m_volume);
    m_engine->play(current, upcoming);
    emit changed();
}

void Player::queueUpcoming()
{
    if (m_index < 0 || m_index >= m_queue.size()) {
        return;
    }
    const int next = m_repeat == Repeat::One && playable(m_queue[m_index].trackId)
        ? m_index
        : nextPlayable(m_index + 1, m_repeat == Repeat::All);
    m_engine->setUpcoming(next >= 0 ? itemFor(m_queue[next]) : PlaybackItem { });
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
    if (m_normalization) {
        return cachedLoudness(track).gainDb();
    }
    if (!m_replayGain) {
        return 0;
    }
    const bool useAlbum = albumContext && !m_shuffle && track.hasAlbumGain;
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

namespace echo {
void Player::trashFiles(const QStringList& requestedPaths)
{
    if (m_trashing || requestedPaths.isEmpty())
        return;
    QStringList paths;
    for (const auto& path : requestedPaths)
        if (!paths.contains(normalizedMusicPath(path)))
            paths << normalizedMusicPath(path);
    ++m_requestRevision;
    m_trashing = true;
    const auto oldIndex = m_index;
    const auto oldPosition = positionMs();
    const auto oldState = state();
    const auto revision = m_playRevision;
    const auto currentPath = currentTrack().path;
    m_cancelAnalysis.store(true);
    const int upcoming = nextPlayable(oldIndex + 1, m_repeat == Repeat::All);
    const bool releasePlayback = paths.contains(currentPath)
        || (upcoming >= 0 && paths.contains(m_library->track(m_queue[upcoming].trackId).path));
    auto perform = [this, paths, oldIndex, oldPosition, oldState, revision, currentPath, releasePlayback] {
        using Results = QVector<QPair<QString, QString>>;
        auto* watcher = new QFutureWatcher<Results>(this);
        connect(watcher, &QFutureWatcher<Results>::finished, this,
            [this, watcher, oldIndex, oldPosition, oldState, revision, currentPath, releasePlayback] {
                const auto results = watcher->result();
                watcher->deleteLater();
                QStringList errors, successes;
                bool removedCurrent = false;
                for (const auto& result : results) {
                    if (result.second.isEmpty()) {
                        successes << result.first;
                        m_library->markMissingPath(result.first);
                        removedCurrent |= result.first == currentPath;
                    } else
                        errors << result.first + QStringLiteral("：") + result.second;
                }
                m_trashing = false;
                m_cancelAnalysis.store(false);
                if (releasePlayback && revision == m_playRevision && oldIndex >= 0 && oldIndex < m_queue.size()) {
                    const int resume = removedCurrent ? nextPlayable(oldIndex + 1, m_repeat == Repeat::All) : oldIndex;
                    if (resume >= 0 && oldState != AudioEngine::State::Stopped) {
                        playIndex(resume, !removedCurrent, oldPosition);
                        if (oldState == AudioEngine::State::Paused)
                            m_engine->pause();
                    }
                }
                scheduleAnalysis();
                emit trashFinished(QStringLiteral("%1 个文件已移到废纸篓，%2 个文件未能处理。歌曲引用会保留。")
                                       .arg(successes.size())
                                       .arg(errors.size())
                    + (successes.isEmpty() ? QString() : QStringLiteral("\n已移到废纸篓：\n") + successes.join(u'\n'))
                    + (errors.isEmpty() ? QString() : QStringLiteral("\n未能处理：\n") + errors.join(u'\n')));
            });
        watcher->setFuture(QtConcurrent::run([paths] {
            Results results;
            for (const auto& path : paths) {
                QFile file(path);
                const bool ok = file.moveToTrash();
                results.push_back({ path, ok ? QString() : file.errorString() });
            }
            return results;
        }));
    };
    auto afterRelease = [this, perform] {
        if (m_analysis.isRunning())
            connect(&m_analysis, &QFutureWatcher<LoudnessResult>::finished, this, perform, Qt::SingleShotConnection);
        else
            perform();
    };
    if (releasePlayback) {
        connect(m_engine, &AudioEngine::filesReleased, this, afterRelease, Qt::SingleShotConnection);
        m_engine->releaseFiles();
    } else
        QTimer::singleShot(0, this, afterRelease);
}
}
