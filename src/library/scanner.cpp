#include "library/scanner.h"

#include "audio/cue.h"
#include "audio/decoder.h"
#include "library/artwork.h"
#include "library/database.h"
#include "library/metadata.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace echo {
namespace {

QString resolveCueFile(const QString& cueDirectory, const QString& fileName)
{
    const QFileInfo direct(QDir(cueDirectory).filePath(fileName));
    if (direct.exists()) {
        return direct.absoluteFilePath();
    }
    QDir directory(cueDirectory);
    const auto entries = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
            return entry.absoluteFilePath();
        }
    }
    return direct.absoluteFilePath();
}

void fillProbe(Track* track)
{
    if (track->sampleRate > 0 && track->durationMs > 0 && track->bitDepth > 0) {
        return;
    }
    Decoder decoder;
    if (!decoder.open(track->path)) {
        return;
    }
    const auto& format = decoder.format();
    if (track->sampleRate <= 0) {
        track->sampleRate = format.sampleRate;
    }
    if (track->channels <= 0) {
        track->channels = format.channels;
    }
    if (track->bitDepth <= 0) {
        track->bitDepth = format.bitsPerSample;
    }
    if (track->durationMs <= 0) {
        track->durationMs = decoder.durationMs();
    }
    if (!track->hasTrackGain && decoder.replayGain().hasTrack) {
        track->hasTrackGain = true;
        track->trackGainDb = decoder.replayGain().trackDb;
        track->trackPeak = decoder.replayGain().trackPeak;
    }
    if (!track->hasAlbumGain && decoder.replayGain().hasAlbum) {
        track->hasAlbumGain = true;
        track->albumGainDb = decoder.replayGain().albumDb;
        track->albumPeak = decoder.replayGain().albumPeak;
    }
}

} // namespace

Scanner::Scanner(QString databasePath, QString artworkDirectory, QObject* parent)
    : QObject(parent)
    , m_databasePath(std::move(databasePath))
    , m_artworkDirectory(std::move(artworkDirectory))
{
}

void Scanner::scanRoot(qint64 rootId, const QString& path)
{
    Database database;
    QString error;
    if (!database.open(m_databasePath, QStringLiteral("scanner"), &error)) {
        emit failed(error);
        return;
    }
    ArtworkCache artwork(m_artworkDirectory);
    QStringList files;
    QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString filePath = iterator.next();
        if (isAudioFile(filePath) || isCueFile(filePath)) {
            files.push_back(filePath);
        }
    }
    database.markRootMissing(rootId);
    QSet<QString> consumedAudio;
    const int total = files.size();
    int done = 0;
    for (const QString& filePath : files) {
        ++done;
        if (done % 25 == 0 || done == total) {
            emit progress(rootId, done, total);
        }
        if (!isCueFile(filePath)) {
            continue;
        }
        QFile cueFile(filePath);
        if (!cueFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString sheet = QString::fromUtf8(cueFile.readAll());
        const auto cueTracks = parseCueSheet(sheet);
        QHash<QString, ScannedTags> cache;
        for (const CueTrack& cueTrack : cueTracks) {
            const QString audioPath = resolveCueFile(QFileInfo(filePath).absolutePath(), cueTrack.file);
            consumedAudio.insert(QFileInfo(audioPath).absoluteFilePath());
            if (!cache.contains(audioPath)) {
                cache.insert(audioPath, readTags(audioPath));
                fillProbe(&cache[audioPath].track);
            }
            Track track = cache.value(audioPath).track;
            track.rootId = rootId;
            track.path = QFileInfo(audioPath).absoluteFilePath();
            track.folder = QFileInfo(audioPath).absolutePath();
            if (!cueTrack.title.isEmpty()) {
                track.title = cueTrack.title;
            }
            if (!cueTrack.performer.isEmpty()) {
                track.artist = cueTrack.performer;
            }
            track.trackNumber = cueTrack.number;
            track.cuePath = QFileInfo(filePath).absoluteFilePath();
            track.cueNumber = cueTrack.number;
            track.cueStartMs = cueTrack.startMs;
            track.cueEndMs = cueTrack.endMs;
            if (track.cueEndMs >= 0) {
                track.durationMs = std::max<qint64>(0, track.cueEndMs - track.cueStartMs);
            }
            if (cueTrack.hasTrackGain) {
                track.hasTrackGain = true;
                track.trackGainDb = cueTrack.trackGainDb;
            }
            if (cueTrack.hasAlbumGain) {
                track.hasAlbumGain = true;
                track.albumGainDb = cueTrack.albumGainDb;
            }
            track.albumKey = makeAlbumKey(track.albumArtist, track.artist, track.album);
            track.artistKey = makeArtistKey(track.artist);
            if (!cache.value(audioPath).artwork.isEmpty()) {
                track.artworkHash = artwork.store(cache.value(audioPath).artwork);
            }
            database.upsertTrack(track);
        }
    }
    for (const QString& filePath : files) {
        if (!isAudioFile(filePath)) {
            continue;
        }
        const QString absolute = QFileInfo(filePath).absoluteFilePath();
        if (consumedAudio.contains(absolute)) {
            continue;
        }
        auto scanned = readTags(absolute);
        if (!scanned.cuesheet.isEmpty()) {
            const auto cueTracks = parseCueSheet(scanned.cuesheet);
            if (!cueTracks.isEmpty()) {
                fillProbe(&scanned.track);
                for (const CueTrack& cueTrack : cueTracks) {
                    Track track = scanned.track;
                    track.rootId = rootId;
                    if (!cueTrack.title.isEmpty()) {
                        track.title = cueTrack.title;
                    }
                    if (!cueTrack.performer.isEmpty()) {
                        track.artist = cueTrack.performer;
                    }
                    track.trackNumber = cueTrack.number;
                    track.cuePath = absolute;
                    track.cueNumber = cueTrack.number;
                    track.cueStartMs = cueTrack.startMs;
                    track.cueEndMs = cueTrack.endMs;
                    if (track.cueEndMs >= 0) {
                        track.durationMs = std::max<qint64>(0, track.cueEndMs - track.cueStartMs);
                    }
                    track.albumKey = makeAlbumKey(track.albumArtist, track.artist, track.album);
                    track.artistKey = makeArtistKey(track.artist);
                    track.artworkHash = artwork.store(scanned.artwork);
                    database.upsertTrack(track);
                }
                consumedAudio.insert(absolute);
                continue;
            }
        }
        fillProbe(&scanned.track);
        scanned.track.rootId = rootId;
        scanned.track.artworkHash = artwork.store(scanned.artwork);
        scanned.track.cueStartMs = -1;
        database.upsertTrack(scanned.track);
    }
    database.deleteStaleCueRows(rootId);
    emit progress(rootId, total, total);
    emit finished(rootId);
}

} // namespace echo
