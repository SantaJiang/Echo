#include "library/types.h"

#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

namespace echo {

QString normalizedMusicPath(const QString& path)
{
    const QFileInfo file(path);
    const auto parent = QFileInfo(file.absolutePath()).canonicalFilePath();
    return QDir::cleanPath(parent.isEmpty() ? file.absoluteFilePath() : QDir(parent).filePath(file.fileName()));
}

QString Track::displayTitle() const
{
    if (!title.trimmed().isEmpty()) {
        return title;
    }
    return QFileInfo(path).completeBaseName();
}

QString Track::displayArtist() const
{
    return artist.trimmed().isEmpty() ? QStringLiteral("未知艺人") : artist;
}

QString Track::displayAlbum() const
{
    return album.trimmed().isEmpty() ? QStringLiteral("未知专辑") : album;
}

QString Track::displayAlbumArtist() const
{
    if (!albumArtist.trimmed().isEmpty()) {
        return albumArtist;
    }
    return displayArtist();
}

QString primaryArtistName(const QString& artist)
{
    static const QRegularExpression separator(
        QStringLiteral(R"(\s+(?:feat\.|ft\.|featuring|with)\s+)"), QRegularExpression::CaseInsensitiveOption);
    const QString primary = artist.split(separator).value(0).trimmed();
    return primary.isEmpty() ? artist.trimmed() : primary;
}

QString makeAlbumKey(const QString& albumArtist, const QString& artist, const QString& album)
{
    const QString owner = primaryArtistName(albumArtist.isEmpty() ? artist : albumArtist).toLower();
    return owner + QChar(0x1f) + album.trimmed().toLower();
}

QString makeArtistKey(const QString& artist)
{
    const QString key = primaryArtistName(artist).toLower();
    return key.isEmpty() ? QStringLiteral("\u0001unknown") : key;
}

bool isAudioFile(const QString& path)
{
    static const QStringList extensions {QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("wav"),
                                          QStringLiteral("aiff"), QStringLiteral("aif"),  QStringLiteral("ape"),
                                          QStringLiteral("wv"),   QStringLiteral("tta"),  QStringLiteral("mpc"),
                                          QStringLiteral("mp+"),  QStringLiteral("dsf"),  QStringLiteral("dff"),
                                          QStringLiteral("dsd"),  QStringLiteral("mp3"),  QStringLiteral("m4a"),
                                          QStringLiteral("aac"),  QStringLiteral("mp4"),  QStringLiteral("opus"),
                                          QStringLiteral("ogg"),  QStringLiteral("oga"),  QStringLiteral("wma")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

bool isCueFile(const QString& path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("cue"), Qt::CaseInsensitive) == 0;
}

} // namespace echo
