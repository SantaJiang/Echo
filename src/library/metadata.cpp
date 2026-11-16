#include "library/metadata.h"

#include "audio/cue.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <taglib/fileref.h>
#include <taglib/tfilestream.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>

namespace echo {
namespace {

QString fromTag(const TagLib::String& value)
{
    return QString::fromUtf8(value.toCString(true));
}

TagLib::String toTag(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return TagLib::String(utf8.constData(), TagLib::String::UTF8);
}

QString propertyText(const TagLib::PropertyMap& map, const char* key)
{
    const auto found = map.find(key);
    if (found == map.end() || found->second.isEmpty()) {
        return {};
    }
    return fromTag(found->second.front());
}

bool parseGain(QString text, double* value)
{
    if (text.isEmpty()) {
        return false;
    }
    text.replace(QStringLiteral("dB"), QString(), Qt::CaseInsensitive);
    bool ok = false;
    const double parsed = text.trimmed().toDouble(&ok);
    if (!ok) {
        return false;
    }
    *value = parsed;
    return true;
}

QByteArray firstPicture(TagLib::File* file)
{
#if TAGLIB_MAJOR_VERSION >= 2
    const auto pictures = file->complexProperties("PICTURE");
    for (const auto& picture : pictures) {
        const auto data = picture.value("data");
        bool ok = false;
        const auto bytes = data.toByteVector(&ok);
        if (ok && !bytes.isEmpty()) {
            return QByteArray(bytes.data(), static_cast<int>(bytes.size()));
        }
    }
#else
    Q_UNUSED(file);
#endif
    return {};
}

QString codecFromSuffix(const QString& path)
{
    return QFileInfo(path).suffix().toUpper();
}

} // namespace

ScannedTags readTags(const QString& path)
{
    ScannedTags scanned;
    scanned.track.path = QFileInfo(path).absoluteFilePath();
    scanned.track.folder = QFileInfo(path).absolutePath();
    scanned.track.codec = codecFromSuffix(path);
    const QFileInfo info(path);
    scanned.track.mtime = info.lastModified().toMSecsSinceEpoch();
    scanned.track.fileSize = info.size();

    TagLib::FileStream stream(path.toUtf8().constData(), true);
    TagLib::FileRef reference(&stream, true, TagLib::AudioProperties::Fast);
    if (reference.isNull() || !reference.file()) {
        scanned.track.title = info.completeBaseName();
        scanned.track.albumKey = makeAlbumKey({}, {}, {});
        scanned.track.artistKey = makeArtistKey({});
        scanned.ok = info.exists();
        return scanned;
    }
    auto* file = reference.file();
    const TagLib::PropertyMap map = file->properties();
    auto& track = scanned.track;
    track.title = propertyText(map, "TITLE");
    track.artist = propertyText(map, "ARTIST");
    track.albumArtist = propertyText(map, "ALBUMARTIST");
    track.album = propertyText(map, "ALBUM");
    track.year = propertyText(map, "DATE").left(4).toInt();
    track.trackNumber = propertyText(map, "TRACKNUMBER").section(u'/', 0, 0).toInt();
    track.discNumber = propertyText(map, "DISCNUMBER").section(u'/', 0, 0).toInt();
    track.lyrics = propertyText(map, "LYRICS");
    if (track.lyrics.isEmpty()) {
        track.lyrics = propertyText(map, "UNSYNCEDLYRICS");
    }
    track.hasTrackGain = parseGain(propertyText(map, "REPLAYGAIN_TRACK_GAIN"), &track.trackGainDb);
    track.hasAlbumGain = parseGain(propertyText(map, "REPLAYGAIN_ALBUM_GAIN"), &track.albumGainDb);
    double peak = 0;
    if (parseGain(propertyText(map, "REPLAYGAIN_TRACK_PEAK"), &peak) && peak > 0) {
        track.trackPeak = peak;
    }
    if (parseGain(propertyText(map, "REPLAYGAIN_ALBUM_PEAK"), &peak) && peak > 0) {
        track.albumPeak = peak;
    }
    if (!track.hasTrackGain) {
        bool ok = false;
        const double r128 = propertyText(map, "R128_TRACK_GAIN").toDouble(&ok);
        if (ok) {
            track.trackGainDb = r128 / 256.0 + 5.0;
            track.hasTrackGain = true;
        }
    }
    scanned.cuesheet = propertyText(map, "CUESHEET");
    if (auto* properties = reference.audioProperties()) {
        track.durationMs = properties->lengthInMilliseconds();
        track.sampleRate = properties->sampleRate();
        track.channels = properties->channels();
        if (track.bitDepth <= 0) {
            track.bitDepth = 0;
        }
    }
    if (track.title.isEmpty()) {
        track.title = info.completeBaseName();
    }
    track.albumKey = makeAlbumKey(track.albumArtist, track.artist, track.album);
    track.artistKey = makeArtistKey(track.artist);
    scanned.artwork = firstPicture(file);
    scanned.ok = true;
    return scanned;
}

bool writeTags(const QString& path, const TagEdit& edit, QString* error)
{
    TagLib::FileStream stream(path.toUtf8().constData(), false);
    TagLib::FileRef reference(&stream, false, TagLib::AudioProperties::Fast);
    if (reference.isNull() || !reference.file()) {
        if (error) {
            *error = QStringLiteral("无法打开标签");
        }
        return false;
    }
    auto* file = reference.file();
    auto map = file->properties();
    const auto put = [&](const char* key, const QString& value) {
        map.replace(key, TagLib::StringList(toTag(value)));
    };
    put("TITLE", edit.title);
    put("ARTIST", edit.artist);
    put("ALBUM", edit.album);
    put("ALBUMARTIST", edit.albumArtist);
    put("TRACKNUMBER", edit.trackNumber > 0 ? QString::number(edit.trackNumber) : QString());
    put("DATE", edit.year > 0 ? QString::number(edit.year) : QString());
    if (edit.editLyrics) {
        put("LYRICS", edit.lyrics);
    }
    file->setProperties(map);
#if TAGLIB_MAJOR_VERSION >= 2
    if (edit.editArtwork) {
        TagLib::List<TagLib::VariantMap> pictures;
        if (!edit.artwork.isEmpty()) {
            TagLib::VariantMap picture;
            picture.insert(TagLib::String("data"), TagLib::ByteVector(edit.artwork.constData(), edit.artwork.size()));
            picture.insert(TagLib::String("mimeType"), toTag(edit.artworkMime));
            picture.insert(TagLib::String("pictureType"), TagLib::String("Front Cover"));
            pictures.append(picture);
        }
        file->setComplexProperties("PICTURE", pictures);
    }
#endif
    if (!file->save()) {
        if (error) {
            *error = QStringLiteral("写入标签失败");
        }
        return false;
    }
    return true;
}

bool rewriteCueEntry(const QString& cuePath, int trackNumber, const QString& title, const QString& performer, QString* error)
{
    QFile file(cuePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法读取 CUE");
        }
        return false;
    }
    const QString original = QString::fromUtf8(file.readAll());
    file.close();
    QStringList lines = original.split(QRegularExpression(QStringLiteral("[\r\n]")));
    bool inTrack = false;
    bool replacedTitle = false;
    bool replacedPerformer = false;
    for (QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("TRACK "), Qt::CaseInsensitive)) {
            const int number = trimmed.section(u' ', 1, 1).toInt();
            inTrack = number == trackNumber;
            replacedTitle = false;
            replacedPerformer = false;
            continue;
        }
        if (!inTrack) {
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("TITLE "), Qt::CaseInsensitive) && !replacedTitle) {
            line = QStringLiteral("    TITLE \"%1\"").arg(title);
            replacedTitle = true;
        } else if (trimmed.startsWith(QStringLiteral("PERFORMER "), Qt::CaseInsensitive) && !replacedPerformer) {
            line = QStringLiteral("    PERFORMER \"%1\"").arg(performer);
            replacedPerformer = true;
        }
    }
    QSaveFile output(cuePath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法写入 CUE");
        }
        return false;
    }
    output.write(lines.join(QStringLiteral("\n")).toUtf8());
    return output.commit();
}

} // namespace echo
