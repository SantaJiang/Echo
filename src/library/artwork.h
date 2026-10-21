#pragma once

#include <QCache>
#include <QObject>
#include <QSet>
#include <QHash>
#include <QElapsedTimer>
#include <QQueue>
#include <QThreadPool>
#include <QImage>
#include <QPixmap>
#include <QString>

namespace echo {

class ArtworkCache : public QObject {
    Q_OBJECT
public:
    explicit ArtworkCache(const QString& directory);

    [[nodiscard]] QString store(const QByteArray& bytes);
    [[nodiscard]] QImage image(const QString& hash) const;
    [[nodiscard]] QPixmap pixmap(const QString& hash, int size);
    [[nodiscard]] QString filePath(const QString& hash) const;

signals:
    void ready(const QString& hash, int size);

private:
    struct Request { QString hash; int size; };
    void startNext();
    QQueue<Request> m_requests;
    int m_active = 0;
    QString m_directory;
    QCache<QString, QPixmap> m_scaled {64 * 1024}; // KiB budget, LRU eviction.
    QSet<QString> m_pending;
    QHash<QString, qint64> m_failed;
    QElapsedTimer m_failureClock;
    QThreadPool m_pool;
};

} // namespace echo
