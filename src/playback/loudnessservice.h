#pragma once

#include "audio/loudness.h"
#include "library/types.h"

#include <QHash>
#include <QSet>
#include <QString>

namespace echo {
class TrackSource;

// Cached loudness measurements. Playback policy stays on Player.
class LoudnessService {
public:
    explicit LoudnessService(TrackSource* source);
    [[nodiscard]] QString key(const Track& track) const;
    [[nodiscard]] LoudnessResult cached(const Track& track) const;
    void store(const QString& key, const LoudnessResult& result);
    void markFailed(const QString& key);
    [[nodiscard]] bool failed(const QString& key) const;
    void forgetTrack(qint64 id);
    void clearKeys();

private:
    TrackSource* m_source;
    mutable QHash<QString, LoudnessResult> m_values;
    mutable QHash<qint64, QString> m_keys;
    QSet<QString> m_failed;
};

} // namespace echo
