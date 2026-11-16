#include "library/artwork.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>

namespace echo {

ArtworkCache::ArtworkCache(const QString& directory)
    : m_directory(directory)
{
    m_pool.setMaxThreadCount(2);
    m_failureClock.start();
    QDir().mkpath(m_directory);
}

QString ArtworkCache::store(const QByteArray& bytes)
{
    if (bytes.isEmpty()) {
        return {};
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    const QString path = filePath(hash);
    QList<int> retrySizes;
    const QString prefix = hash + QLatin1Char(':');
    for (const auto& key : m_failed.keys())
        if (key.startsWith(prefix)) retrySizes << key.mid(prefix.size()).toInt();
    bool stored = QFile::exists(path);
    if (!stored || !retrySizes.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        stored = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    }
    if (stored) {
        for (const int size : retrySizes) {
            m_failed.remove(hash + QLatin1Char(':') + QString::number(size));
            (void)pixmap(hash, size);
        }
    }
    return hash;
}

QImage ArtworkCache::image(const QString& hash) const
{
    if (hash.isEmpty()) {
        return {};
    }
    QImage image;
    image.load(filePath(hash));
    return image;
}

QPixmap ArtworkCache::pixmap(const QString& hash, int size)
{
    if (hash.isEmpty()) {
        return {};
    }
    size = std::clamp(size, 1, 2048);
    const QString key = hash + QLatin1Char(':') + QString::number(size);
    if (const auto* found = m_scaled.object(key)) return *found;
    if (const auto failed = m_failed.find(key); failed != m_failed.end()) {
        if (m_failureClock.elapsed() - failed.value() < 3000) return {};
        m_failed.erase(failed);
    }
    if (m_pending.contains(key)) return {};
    m_pending.insert(key);
    // Keep lightweight requests queued; never lose a one-shot player or dialog request.
    if (size == 320) m_requests.enqueue({hash, size});
    else m_requests.prepend({hash, size});
    startNext();
    return {};
}

void ArtworkCache::startNext()
{
    if (m_active >= 2 || m_requests.isEmpty()) return;
    const auto [hash, size] = m_requests.dequeue();
    const QString key = hash + QLatin1Char(':') + QString::number(size);
    const QString path = filePath(hash);
    ++m_active;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key, hash, size] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        m_pending.remove(key);
        --m_active;
        if (!image.isNull()) {
            const QPixmap pixmap = QPixmap::fromImage(image);
            const qint64 bytes = qint64(pixmap.width()) * pixmap.height() * pixmap.depth() / 8;
            m_scaled.insert(key, new QPixmap(pixmap), std::max(1, int((bytes + 1023) / 1024)));
        } else {
            if (m_failed.size() >= 256) m_failed.clear();
            m_failed.insert(key, m_failureClock.elapsed());
        }
        startNext();
        emit ready(hash, size);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [path, size] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize dimensions = reader.size();
        if (dimensions.isValid()) {
            QSize scaled = dimensions.scaled(size, size, Qt::KeepAspectRatioByExpanding);
            // Panoramic artwork must not turn a thumbnail request into a huge allocation.
            if (scaled.width() > size * 4 || scaled.height() > size * 4)
                scaled = dimensions.scaled(size * 4, size * 4, Qt::KeepAspectRatio);
            reader.setScaledSize(scaled.expandedTo(QSize(1, 1)));
        }
        return reader.read();
    }));
    startNext();
}

QString ArtworkCache::filePath(const QString& hash) const
{
    const QString prefix = hash.left(2);
    return m_directory + QLatin1Char('/') + prefix + QLatin1Char('/') + hash;
}

} // namespace echo
