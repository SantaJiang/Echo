#pragma once

#include "library/types.h"

#include <QObject>
#include <QVector>

namespace echo {

// Narrow library surface used by playback. Keeps the player testable without the
// scanning, playlist and artwork responsibilities of Library.
class TrackSource : public QObject {
    Q_OBJECT
public:
    explicit TrackSource(QObject* parent = nullptr) : QObject(parent) {}
    ~TrackSource() override = default;

    [[nodiscard]] virtual Track track(qint64 id) const = 0;
    [[nodiscard]] virtual QVector<Track> tracks(const QVector<qint64>& ids) const = 0;
    [[nodiscard]] virtual QString setting(const QString& key, const QString& fallback = {}) const = 0;
    virtual void setSetting(const QString& key, const QString& value) = 0;
    virtual void markMissingPath(const QString& path) = 0;

signals:
    void changed();
    void sourceChanged(qint64 id);
};

} // namespace echo
