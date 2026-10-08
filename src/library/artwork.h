#pragma once

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QString>

namespace echo {

class ArtworkCache {
public:
    explicit ArtworkCache(const QString& directory);

    [[nodiscard]] QString store(const QByteArray& bytes);
    [[nodiscard]] QImage image(const QString& hash) const;
    [[nodiscard]] QPixmap pixmap(const QString& hash, int size);
    [[nodiscard]] QString filePath(const QString& hash) const;

private:
    QString m_directory;
    QHash<QString, QPixmap> m_scaled;
};

} // namespace echo
