#include "library/scanner.h"

#include "audio/cue.h"
#include "audio/decoder.h"
#include "library/artwork.h"
#include "library/database.h"
#include "library/metadata.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <filesystem>
#include <QStorageInfo>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <memory>

namespace echo {
namespace {

QString resolveCueFile(const QString& cueDirectory, const QString& fileName)
{
    const QFileInfo direct(QDir(cueDirectory).filePath(fileName));
    if (direct.exists()) {
        return normalizedMusicPath(direct.absoluteFilePath());
    }
    QDir directory(cueDirectory);
    const auto entries = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
            return normalizedMusicPath(entry.absoluteFilePath());
        }
    }
    return normalizedMusicPath(direct.absoluteFilePath());
}

bool fillProbe(Track* track)
{
    if (track->sampleRate > 0 && track->durationMs > 0 && track->bitDepth > 0) {
        return true;
    }
    Decoder decoder;
    if (!decoder.open(track->path))
        return false;
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
    return true;
}

} // namespace

struct ScanSession {
    Scanner* scanner = nullptr;
    QString databasePath;
    QString artworkDirectory;
    Database database;
    qint64 rootId = 0;
    QString path;
    LibraryRoot root;
    std::unique_ptr<ArtworkCache> artwork;
    QVector<Track> output;
    QVector<Track> previous;
    QHash<QString, QVector<Track>> byPath;
    QHash<QString, QVector<Track>> byCue;
    int parsed = 0;
    QJsonObject oldSignatures;
    QJsonObject signatures;
    bool missingArtwork = false;
    QSet<QString> failedPaths;
    QStringList files;
    QSet<QString> consumedAudio;
    int total = 0;
    bool retry = false;

    void unavailable(const QString& status, const QString& message)
    {
        database.setRootStatus(rootId, path, root.revision, status, message);
        scanner->warning(message);
        scanner->finished(rootId, root.status != status || root.message != message);
    }
    ScannedTags tags(const QString& file)
    {
        ++parsed;
        return readTags(file);
    }
    static QString stamp(const QString& file)
    {
        const QFileInfo info(file);
        return QString::number(info.lastModified().toMSecsSinceEpoch()) + u':' + QString::number(info.size());
    }
    bool unchanged(const Track& track)
    {
        const QFileInfo info(track.path);
        const bool coverAvailable = track.artworkHash.isEmpty() || QFileInfo::exists(artwork->filePath(track.artworkHash));
        missingArtwork |= !coverAvailable;
        return coverAvailable && info.isFile() && info.isReadable() && info.size() == track.fileSize
            && info.lastModified().toMSecsSinceEpoch() == track.mtime;
    }
    void preserve(const QString& file)
    {
        if (failedPaths.contains(file))
            return;
        failedPaths.insert(file);
        for (auto old : byPath.value(file)) {
            old.missing = true;
            output.push_back(old);
        }
    }
    bool enumerateFiles()
    {
    // std::filesystem exposes enumeration errors; an interrupted enumeration must never mark files missing.
    std::error_code enumerationError;
    const auto utf8Path = path.toUtf8();
    const auto nativePath
        = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path.constData()), utf8Path.size()));
    std::filesystem::directory_iterator iterator(nativePath, enumerationError), end;
    while (!enumerationError && iterator != end) {
        const auto bytes = iterator->path().u8string();
        const QString filePath
            = normalizedMusicPath(QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        const QFileInfo info(filePath);
        if (info.isFile() && (isAudioFile(filePath) || isCueFile(filePath))) {
            files.push_back(filePath);
            signatures.insert(filePath, stamp(filePath));
        }
        iterator.increment(enumerationError);
    }
    if (enumerationError) {
        unavailable(QStringLiteral("unavailable"), QString::fromStdString(enumerationError.message()));
        return false;
    }
        return true;
    }
    void readExternalCues()
    {
    total = files.size();
    int done = 0;
    for (const QString& filePath : files) {
        ++done;
        if (done % 25 == 0 || done == total) {
            scanner->progress(rootId, done, total);
        }
        if (!isCueFile(filePath)) {
            continue;
        }
        QVector<Track> cached;
        bool reusable = oldSignatures.value(filePath) == signatures.value(filePath);
        for (auto old : byCue.value(filePath))
            if (!old.missing) {
                reusable &= unchanged(old);
                old.missing = false;
                cached.push_back(old);
            }
        if (reusable && !cached.isEmpty()) {
            for (const auto& old : cached) {
                output.push_back(old);
                consumedAudio.insert(old.path);
            }
            continue;
        }
        QFile cueFile(filePath);
        if (!cueFile.open(QIODevice::ReadOnly)) {
            for (auto old : byCue.value(filePath)) {
                old.missing = true;
                output.push_back(old);
                consumedAudio.insert(old.path);
            }
            continue;
        }
        const QString sheet = QString::fromUtf8(cueFile.readAll());
        const auto cueTracks = parseCueSheet(sheet);
        QHash<QString, ScannedTags> cache;
        for (const CueTrack& cueTrack : cueTracks) {
            const QString audioPath = resolveCueFile(QFileInfo(filePath).absolutePath(), cueTrack.file);
            if (QFileInfo(audioPath).absolutePath() != QFileInfo(path).canonicalFilePath()) {
                scanner->warning(QStringLiteral("CUE 引用了当前文件夹之外的音频，未收录：%1").arg(audioPath));
                continue;
            }
            consumedAudio.insert(QFileInfo(audioPath).absoluteFilePath());
            if (!cache.contains(audioPath)) {
                cache.insert(audioPath, tags(audioPath));
                if (cache[audioPath].ok)
                    cache[audioPath].ok = fillProbe(&cache[audioPath].track);
            }
            if (!cache.value(audioPath).ok || !QFileInfo(audioPath).isFile()) {
                preserve(audioPath);
                continue;
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
                track.artworkHash = artwork->store(cache.value(audioPath).artwork);
            }
            output.push_back(track);
        }
    }
    }
    void readAudioFiles()
    {
    for (const QString& filePath : files) {
        if (!isAudioFile(filePath)) {
            continue;
        }
        const QString absolute = QFileInfo(filePath).absoluteFilePath();
        if (consumedAudio.contains(absolute)) {
            continue;
        }
        QVector<Track> cached;
        bool reusable = true;
        for (auto old : byPath.value(absolute))
            if (!old.missing && (old.cuePath.isEmpty() || old.cuePath == absolute)) {
                reusable &= unchanged(old);
                old.missing = false;
                cached.push_back(old);
            }
        if (reusable && !cached.isEmpty()) {
            output += cached;
            continue;
        }
        auto scanned = tags(absolute);
        if (!scanned.ok) {
            preserve(absolute);
            continue;
        }
        if (!scanned.cuesheet.isEmpty()) {
            const auto cueTracks = parseCueSheet(scanned.cuesheet);
            if (!cueTracks.isEmpty()) {
                if (!fillProbe(&scanned.track)) {
                    preserve(absolute);
                    continue;
                }
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
                    track.artworkHash = artwork->store(scanned.artwork);
                    output.push_back(track);
                }
                consumedAudio.insert(absolute);
                continue;
            }
        }
        if (!fillProbe(&scanned.track)) {
            preserve(absolute);
            continue;
        }
        scanned.track.rootId = rootId;
        scanned.track.artworkHash = artwork->store(scanned.artwork);
        scanned.track.cueStartMs = -1;
        output.push_back(scanned.track);
    }
    }
    void stabilizeTracks()
    {
    // Do not publish a new record from a file still being copied. Existing records remain cached until a retry.
    QVector<Track> stable;
    for (auto track : output) {
        const bool audioExists = QFileInfo::exists(track.path);
        const bool sidecarExists = track.cuePath.isEmpty() || QFileInfo::exists(track.cuePath);
        if (audioExists
            && (signatures.value(track.path).toString() != stamp(track.path)
                || (!track.cuePath.isEmpty() && signatures.contains(track.cuePath)
                    && signatures.value(track.cuePath).toString() != stamp(track.cuePath)))) {
            retry = true;
            for (const auto& old : byPath.value(track.path))
                if (old.cueStartMs == track.cueStartMs) {
                    stable << old;
                    break;
                }
        } else {
            track.missing |= !audioExists || !sidecarExists;
            stable << track;
        }
    }
    output = std::move(stable);
    }
    bool publishScan(QString* error)
    {
    if (!failedPaths.isEmpty())
        scanner->warning(QStringLiteral("%1 个文件暂时无法读取，已保留之前的资料。").arg(failedPaths.size()));
    if (retry)
        scanner->retryRequested(rootId);
    if (!QFileInfo(path).isDir()) {
        unavailable(QStringLiteral("unavailable"), QStringLiteral("扫描期间目录已不可用"));
        return false;
    }
    if (!database.commitScan(rootId, path, root.revision, output,
            QString::fromUtf8(QJsonDocument(signatures).toJson(QJsonDocument::Compact)), error)) {
        scanner->failed(*error);
        return false;
    }
    bool changed = missingArtwork || root.status != QStringLiteral("available");
    QSet<QString> present;
    for (auto track : output) {
        const auto key = track.path + u'\n' + QString::number(track.cueStartMs);
        present.insert(key);
        bool found = false;
        for (const auto& old : byPath.value(track.path))
            if (old.cueStartMs == track.cueStartMs) {
                track.id = old.id;
                changed |= track != old;
                found = true;
                break;
            }
        changed |= !found;
    }
    for (const auto& old : previous)
        if (!old.missing && !present.contains(old.path + u'\n' + QString::number(old.cueStartMs)))
            changed = true;
    scanner->statistics(rootId, parsed);
    scanner->progress(rootId, total, total);
    scanner->finished(rootId, changed);
        return true;
    }
};


Scanner::Scanner(QString databasePath, QString artworkDirectory, QObject* parent)
    : QObject(parent)
    , m_databasePath(std::move(databasePath))
    , m_artworkDirectory(std::move(artworkDirectory))
{
}

void Scanner::scanRoot(qint64 rootId, const QString& path)
{
    ScanSession session;
    session.scanner = this;
    session.databasePath = m_databasePath;
    session.artworkDirectory = m_artworkDirectory;
    session.rootId = rootId;
    session.path = path;
    QString error;
    if (!session.database.open(session.databasePath, QStringLiteral("scanner"), &error)) {
        emit failed(error);
        return;
    }
    session.root = session.database.root(rootId);
    if (!session.root.id || !session.root.active || session.root.path != path) {
        emit finished(rootId, false);
        return;
    }
    if (!QFileInfo(path).isDir()) {
        const bool offline = !session.root.mountPath.isEmpty() && session.root.mountPath != QStringLiteral("/")
            && (!QFileInfo(session.root.mountPath).isDir() || !QStorageInfo(session.root.mountPath).isReady());
        session.unavailable(offline ? QStringLiteral("offline") : QStringLiteral("missing"),
            QStringLiteral("暂时找不到目录：%1").arg(path));
        return;
    }
    if (!QFileInfo(path).isReadable()) {
        session.unavailable(QStringLiteral("permission"), QStringLiteral("无访问权限：%1").arg(path));
        return;
    }
    session.artwork = std::make_unique<ArtworkCache>(session.artworkDirectory);
    session.previous = session.database.queryTracks(QStringLiteral("WHERE root_id=?"), { rootId }, { });
    for (const auto& old : session.previous) {
        session.byPath[old.path] << old;
        if (!old.cuePath.isEmpty())
            session.byCue[old.cuePath] << old;
    }
    session.oldSignatures = QJsonDocument::fromJson(
        session.database.setting(QStringLiteral("scan_signature_%1").arg(rootId)).toUtf8()).object();
    if (!session.enumerateFiles())
        return;
    session.readExternalCues();
    session.readAudioFiles();
    session.stabilizeTracks();
    session.publishScan(&error);
}

} // namespace echo

