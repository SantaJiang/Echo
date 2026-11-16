#pragma once

#include "audio/engine.h"
#include "audio/loudness.h"
#include "playback/loudnessservice.h"
#include <QFutureWatcher>
#include <QHash>
#include <atomic>
#include <optional>
#include "library/library.h"

#include <QObject>
#include <QVector>

namespace echo {

class Player : public QObject {
    Q_OBJECT

public:
    enum class Repeat { Off, All, One };

    Player(TrackSource* library, AudioEngine* engine, QObject* parent = nullptr);

    ~Player() override;
    void setNormalizationEnabled(bool enabled);
    [[nodiscard]] bool normalizationEnabled() const { return m_normalization; }
    [[nodiscard]] QString normalizationStatus() const;

    [[nodiscard]] bool replaceContextQueue() const;
    void setReplaceContextQueue(bool enabled);
    void playSinglePreservingQueue(qint64 id);
    void activateQueueRow(int index);
    void trashFiles(const QStringList& paths);
    void playTracks(const QVector<qint64>& ids, int startIndex, bool albumContext, std::optional<bool> shuffle = { });
    void toggle();
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(float slider);
    void setFadingEnabled(bool enabled);
    [[nodiscard]] bool fadingEnabled() const { return m_fading; }
    void setExclusive(bool exclusive);
    void setSpatialEnabled(bool enabled);
    [[nodiscard]] bool spatialEnabled() const { return m_spatial; }
    [[nodiscard]] QString spatialStatus() const;
    void setReplayGainEnabled(bool enabled);
    void setShuffle(bool shuffle);
    void setRepeat(Repeat repeat);
    void enqueueNext(const QVector<qint64>& ids);
    void enqueue(const QVector<qint64>& ids);
    void moveQueue(int from, int to);
    void removeQueueRow(int row);
    void clearUpcoming();
    void restoreSession();
    void saveSession() const;

    struct Entry {
        qint64 token = 0;
        qint64 trackId = 0;
        bool albumContext = false;
        bool operator==(const Entry&) const = default;
    };

    [[nodiscard]] QVector<Entry> queue() const { return m_queue; }
    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] Track currentTrack() const;
    [[nodiscard]] float volume() const { return m_volume; }
    [[nodiscard]] bool shuffle() const { return m_shuffle; }
    [[nodiscard]] Repeat repeat() const { return m_repeat; }
    [[nodiscard]] bool replayGainEnabled() const { return m_replayGain; }
    [[nodiscard]] bool exclusive() const { return m_exclusive; }
    [[nodiscard]] qint64 positionMs() const;
    [[nodiscard]] AudioEngine::State state() const;

signals:
    void changed();
    void normalizationChanged();
    void playbackMessage(const QString& message);
    void trackUnavailable(qint64 id);
    void trashFinished(const QString& report);
    void spatialChanged();

private:
    [[nodiscard]] bool playable(qint64 id) const;
    [[nodiscard]] int nextPlayable(int from, bool wrap) const;
    void playIndex(int index, bool resumePosition, qint64 positionMs);
    void queueUpcoming();
    [[nodiscard]] PlaybackItem itemFor(const Entry& entry) const;
    [[nodiscard]] float gainFor(const Track& track, bool albumContext) const;
    void rememberHistory();
    void scheduleAnalysis();
    void updateNormalizationGains(qint64 trackId = 0);
    [[nodiscard]] Track queueTrack(qint64 id) const;
    [[nodiscard]] QString loudnessKey(const Track& track) const;
    [[nodiscard]] LoudnessResult cachedLoudness(const Track& track) const;

    TrackSource* m_library = nullptr;
    LoudnessService m_loudnessService;
    AudioEngine* m_engine = nullptr;
    QVector<Entry> m_queue;
    QVector<qint64> m_history;
    int m_index = -1;
    qint64 m_nextToken = 1;
    float m_volume = 1;
    bool m_fading = true;
    bool m_shuffle = false;
    bool m_replayGain = true;
    bool m_exclusive = false;
    bool m_spatial = false;
    QString m_spatialStatus;
    Repeat m_repeat = Repeat::Off;
    bool m_normalization = false;
    QFutureWatcher<LoudnessResult> m_analysis;
    bool m_analysisPending = false;
    bool m_analysisDirty = true;
    QVector<Entry> m_analysisEntries;
    QVector<Track> m_analysisTracks;
    int m_analysisCursor = 0;
    mutable QHash<qint64, Track> m_queueTracks;
    std::atomic<bool> m_cancelAnalysis {false};
    bool m_trashing = false;
    qint64 m_requestRevision = 0;
    qint64 m_playRevision = 0;
    bool m_skipAfterError = false;
};

} // namespace echo
