#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace echo {

struct Track {
    qint64 id = 0;
    qint64 rootId = 0;
    qint64 listEntryId = 0;
    int playlistPosition = -1;
    QString path;
    QString folder;
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    QString albumKey;
    QString artistKey;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    qint64 durationMs = 0;
    int sampleRate = 0;
    int bitDepth = 0;
    int channels = 0;
    QString codec;
    bool hasTrackGain = false;
    bool hasAlbumGain = false;
    double trackGainDb = 0;
    double albumGainDb = 0;
    double trackPeak = 1;
    double albumPeak = 1;
    QString lyrics;
    QString artworkHash;
    qint64 mtime = 0;
    qint64 fileSize = 0;
    bool missing = false;
    QString cuePath;
    int cueNumber = 0;
    qint64 cueStartMs = -1;
    qint64 cueEndMs = -1;

    bool operator==(const Track&) const = default;
    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] QString displayArtist() const;
    [[nodiscard]] QString displayAlbum() const;
};

struct Album {
    QString key;
    QString title;
    QString artist;
    int year = 0;
    int tracks = 0;
    QString artworkHash;
};

struct Artist {
    QString key;
    QString name;
    int albums = 0;
    int tracks = 0;
};

struct LibraryRoot {
    qint64 id = 0;
    QString path;
    QString name;
    QString status = QStringLiteral("available");
    QString mountPath;
    QString message;
    qint64 revision = 0;
    qint64 checkedAt = 0;
    bool active = true;
    int tracks = 0;
    int availableTracks = 0;
};

struct Playlist {
    qint64 id = 0;
    QString name;
    qint64 folderId = 0;
    int sortOrder = 0;
    QString systemKey;
    [[nodiscard]] bool isFavorites() const { return systemKey == QLatin1String("favorites"); }
};

struct PlaylistFolder {
    qint64 id = 0;
    QString name;
    qint64 parentId = 0;
    int sortOrder = 0;
};

struct TagEdit {
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    int trackNumber = 0;
    int year = 0;
    bool editLyrics = false;
    QString lyrics;
    bool editArtwork = false;
    QByteArray artwork;
    QString artworkMime = QStringLiteral("image/jpeg");
};

[[nodiscard]] QString normalizedMusicPath(const QString& path);
[[nodiscard]] QString primaryArtistName(const QString& artist);
[[nodiscard]] QString makeAlbumKey(const QString& albumArtist, const QString& artist, const QString& album);
[[nodiscard]] QString makeArtistKey(const QString& artist);
[[nodiscard]] bool isAudioFile(const QString& path);
[[nodiscard]] bool isCueFile(const QString& path);

} // namespace echo

Q_DECLARE_METATYPE(echo::Track)
