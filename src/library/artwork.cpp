#include "library/artwork.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace echo {

ArtworkCache::ArtworkCache(const QString& directory)
    : m_directory(directory)
{
    QDir().mkpath(m_directory);
}

QString ArtworkCache::store(const QByteArray& bytes)
{
    if (bytes.isEmpty()) {
        return {};
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    const QString path = filePath(hash);
    if (!QFile::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
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
    const QString key = hash + QLatin1Char(':') + QString::number(size);
    if (const auto found = m_scaled.constFind(key); found != m_scaled.cend()) {
        return found.value();
    }
    QImage image = this->image(hash);
    if (image.isNull()) {
        return {};
    }
    const QPixmap pixmap = QPixmap::fromImage(image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    if (m_scaled.size() > 256) {
        m_scaled.clear();
    }
    m_scaled.insert(key, pixmap);
    return pixmap;
}

QString ArtworkCache::filePath(const QString& hash) const
{
    const QString prefix = hash.left(2);
    return m_directory + QLatin1Char('/') + prefix + QLatin1Char('/') + hash;
}

} // namespace echo
