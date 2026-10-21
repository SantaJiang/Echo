#pragma once

#include "audio/format.h"
#include "audio/output.h"

#include <QObject>
#include <QHash>
#include <atomic>

namespace echo {

class AudioEngine : public QObject {
    Q_OBJECT

public:
    enum class State { Stopped, Playing, Paused };

    explicit AudioEngine(QObject* parent = nullptr);
    AudioEngine(std::unique_ptr<AudioOutput> output, QObject* parent = nullptr);
    ~AudioEngine() override;

    void play(const PlaybackItem& current, const PlaybackItem& upcoming);
    void setUpcoming(const PlaybackItem& upcoming);
    void pause();
    void resume();
    void stop();
    void releaseFiles();
    void seekMs(qint64 positionMs);
    void setVolume(float slider);
    void setExclusive(bool exclusive);
    void setSpatialEnabled(bool enabled);
    void setTrackGain(qint64 token, float gainDb);
    void setTrackGains(const QHash<qint64, float>& gains);

    [[nodiscard]] State state() const;
    [[nodiscard]] qint64 positionMs() const;
    [[nodiscard]] qint64 activeToken() const;
    [[nodiscard]] DeviceFormat deviceFormat() const;

signals:
    void filesReleased();
    void positionChanged();
    void spatialStatusChanged(const QString& status);
    void stateChanged(int state);
    void trackStarted(qint64 token);
    void finished();
    void errorOccurred(const QString& message);
    void exclusiveFallback(const QString& reason);
    void deviceOpened(int sourceRate, int sourceBits, bool sourceFloat, int deviceRate, bool exclusive);

private:
    struct Worker;
    Worker* m_worker = nullptr;
    std::atomic<int> m_state {static_cast<int>(State::Stopped)};
    std::atomic<qint64> m_positionMs {0};
    std::atomic<qint64> m_activeToken {0};
    std::atomic<int> m_deviceRate {0};
    std::atomic<int> m_deviceBits {0};
    std::atomic<bool> m_deviceExclusive {false};
};

} // namespace echo
