#pragma once

#include "audio/engine.h"
#include "library/library.h"

#include <QObject>
#include <QVector>

namespace echo {

class Player : public QObject {
    Q_OBJECT

public:
    enum class Repeat { Off, All, One };

    Player(Library* library, AudioEngine* engine, QObject* parent = nullptr);

    void playTracks(const QVector<qint64>& ids, int startIndex, bool albumContext);
    void toggle();
    void next();
    void previous();
    void seek(qint64 positionMs);
    void setVolume(float slider);
    void setExclusive(bool exclusive);
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

private:
    void playIndex(int index, bool resumePosition, qint64 positionMs);
    void queueUpcoming();
    [[nodiscard]] PlaybackItem itemFor(const Entry& entry) const;
    [[nodiscard]] float gainFor(const Track& track, bool albumContext) const;
    void rememberHistory();

    Library* m_library = nullptr;
    AudioEngine* m_engine = nullptr;
    QVector<Entry> m_queue;
    QVector<qint64> m_history;
    int m_index = -1;
    qint64 m_nextToken = 1;
    float m_volume = 1;
    bool m_shuffle = false;
    bool m_replayGain = true;
    bool m_exclusive = false;
    Repeat m_repeat = Repeat::Off;
    bool m_advancing = false;
    bool m_skipAfterError = false;
};

} // namespace echo
