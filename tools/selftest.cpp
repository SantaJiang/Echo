#include "audio/convert.h"
#include "audio/cue.h"
#include "audio/decoder.h"
#include "audio/loudness.h"
#include "library/database.h"
#include "library/scanner.h"
#include "library/types.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL " << message << "\n";
        ++failures;
    }
}

QString ffmpeg(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("ffmpeg"), arguments);
    process.waitForFinished(20000);
    return QString::fromUtf8(process.readAllStandardError());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QString cueError;
    const auto cue = echo::parseCueSheet(QStringLiteral(
                                              "PERFORMER \"Album Artist\"\n"
                                              "TITLE \"Album\"\n"
                                              "FILE \"album.flac\" WAVE\n"
                                              "  TRACK 01 AUDIO\n"
                                              "    TITLE \"One\"\n"
                                              "    PERFORMER \"A\"\n"
                                              "    INDEX 01 00:00:00\n"
                                              "  TRACK 02 AUDIO\n"
                                              "    TITLE \"Two\"\n"
                                              "    INDEX 01 00:03:00\n"),
                                          &cueError);
    expect(cueError.isEmpty(), "cue error");
    expect(cue.size() == 2, "cue count");
    expect(cue.size() == 2 && cue[0].endMs == 3000, "cue end");
    expect(cue.size() == 2 && cue[1].endMs == -1, "cue last end");
    expect(cue.size() == 2 && cue[1].performer == QLatin1String("Album Artist"), "cue performer inherit");

    QTemporaryDir dir;
    expect(dir.isValid(), "temp dir");
    const QString wav = dir.filePath(QStringLiteral("tone.wav"));
    const QString flac = dir.filePath(QStringLiteral("tone.flac"));
    const QString mp3 = dir.filePath(QStringLiteral("tone.mp3"));
    const QString raw = dir.filePath(QStringLiteral("tone.s16"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("sine=frequency=440:duration=1:sample_rate=44100"), QStringLiteral("-c:a"),
            QStringLiteral("pcm_s16le"), wav});
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-i"), wav, QStringLiteral("-c:a"), QStringLiteral("flac"), flac});
    // Measure files without ReplayGain tags, and verify the audible result rather than just the setting.
    const QString quiet = dir.filePath(QStringLiteral("quiet.wav"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-i"), wav, QStringLiteral("-af"), QStringLiteral("volume=0.25"), quiet});
    echo::PlaybackItem loudItem;
    loudItem.path = wav;
    echo::PlaybackItem quietItem;
    quietItem.path = quiet;
    const auto loudness = echo::analyzeLoudness(loudItem);
    const auto quietness = echo::analyzeLoudness(quietItem);
    expect(loudness.valid && quietness.valid, "analyze untagged tracks");
    expect(std::abs(loudness.lufs - quietness.lufs - 12.0412) < 0.1, "measure relative loudness");
    expect(std::abs(loudness.lufs + loudness.gainDb() - quietness.lufs - quietness.gainDb()) < 0.1,
           "normalized tracks have matching loudness");
    expect(loudness.peak * std::pow(10., loudness.gainDb() / 20.) <= std::pow(10., -1. / 20.) + 0.0001,
           "normalization reserves true peak headroom");
    const QString balanced = dir.filePath(QStringLiteral("balanced.wav"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-i"), quiet, QStringLiteral("-af"),
            QStringLiteral("volume=%1dB").arg(quietness.gainDb()), balanced});
    quietItem.path = balanced;
    const auto balancedResult = echo::analyzeLoudness(quietItem);
    expect(balancedResult.valid && std::abs(balancedResult.lufs + 18.) < 0.1, "rendered audio reaches target LUFS");
    expect(!echo::analyzeLoudness(loudItem, [] { return true; }).valid, "analysis cancellation");
    loudItem.path = dir.filePath(QStringLiteral("missing.wav"));
    expect(!echo::analyzeLoudness(loudItem).valid, "analysis missing file");
    loudItem.path = wav;
    loudItem.startMs = 200;
    loudItem.endMs = 800;
    expect(echo::analyzeLoudness(loudItem).valid, "analyze CUE segment");
    const QString silence = dir.filePath(QStringLiteral("silence.wav"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("anullsrc=r=44100:cl=stereo:d=1"), silence});
    loudItem.path = silence;
    loudItem.startMs = 0;
    loudItem.endMs = -1;
    const auto silentResult = echo::analyzeLoudness(loudItem);
    expect(silentResult.valid && silentResult.gainDb() == 0, "silence retains unity gain");
    const echo::LoudnessResult highPeak {true, -30, 0.95};
    expect(highPeak.gainDb() < 0, "peak protection overrides requested amplification");
    const QString lfe = dir.filePath(QStringLiteral("lfe.wav"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("aevalsrc=0|0|0|0.5*sin(2*PI*50*t)|0|0:s=48000:d=1:c=5.1"),
            QStringLiteral("-c:a"), QStringLiteral("pcm_f32le"), lfe});
    loudItem.path = lfe;
    const auto lfeResult = echo::analyzeLoudness(loudItem);
    expect(lfeResult.valid && lfeResult.lufs < -70, "LFE is excluded from integrated loudness");
    expect(lfeResult.peak > .49 && lfeResult.gainDb() == 0, "LFE peak is still measured without amplifying silence");


    ffmpeg({QStringLiteral("-y"), QStringLiteral("-i"), wav, QStringLiteral("-c:a"), QStringLiteral("libmp3lame"),
            QStringLiteral("-q:a"), QStringLiteral("2"), mp3});
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-i"), wav, QStringLiteral("-f"), QStringLiteral("s16le"), raw});
    QFile rawFile(raw);
    expect(rawFile.open(QIODevice::ReadOnly), "raw open");
    const QByteArray expected = rawFile.readAll();

    echo::Decoder flacDecoder;
    QString error;
    expect(flacDecoder.open(flac, &error), "flac open");
    const int frameBytes = std::max(1, flacDecoder.format().bytesPerFrame());
    std::vector<uint8_t> decoded(static_cast<size_t>(expected.size()) + static_cast<size_t>(frameBytes) * 8192);
    qint64 got = 0;
    while (true) {
        const int read = flacDecoder.read(decoded.data() + static_cast<size_t>(got) * static_cast<size_t>(frameBytes), 2048);
        if (read <= 0) {
            break;
        }
        got += read;
    }
    const auto decodedBytes = got * frameBytes;
    expect(flacDecoder.format().sampleRate == 44100, "flac rate");
    expect(flacDecoder.format().bitsPerSample == 16, "flac bits");
    expect(decodedBytes == expected.size(), "flac length");
    expect(decodedBytes == expected.size()
               && std::memcmp(decoded.data(), expected.constData(), static_cast<size_t>(expected.size())) == 0,
           "flac bit exact");

    echo::Decoder mp3Decoder;
    expect(mp3Decoder.open(mp3, &error), "mp3 open");
    qint64 mp3Frames = 0;
    std::vector<uint8_t> scratch(8192);
    while (true) {
        const int read = mp3Decoder.read(scratch.data(), 1024);
        if (read <= 0) {
            break;
        }
        mp3Frames += read;
    }
    const double mp3Seconds = static_cast<double>(mp3Frames) / mp3Decoder.format().sampleRate;
    expect(std::fabs(mp3Seconds - 1.0) < 0.08, "mp3 duration");

    QFile cueFile(dir.filePath(QStringLiteral("tone.cue")));
    expect(cueFile.open(QIODevice::WriteOnly), "cue fixture open");
    cueFile.write("PERFORMER \"Tester\"\nTITLE \"Tones\"\nFILE \"tone.flac\" WAVE\n"
                  "  TRACK 01 AUDIO\n    TITLE \"First\"\n    INDEX 01 00:00:00\n"
                  "  TRACK 02 AUDIO\n    TITLE \"Second\"\n    INDEX 01 00:00:40\n");
    cueFile.close();

    echo::Database database;
    const QString dbPath = dir.filePath(QStringLiteral("library.sqlite"));
    {
        auto legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy"));
        legacy.setDatabaseName(dbPath);
        expect(legacy.open(), "legacy db open");
        QSqlQuery query(legacy);
        expect(query.exec(QStringLiteral("CREATE TABLE playlists(id INTEGER PRIMARY KEY, name TEXT NOT NULL, folder_id INTEGER, sort_order INTEGER NOT NULL DEFAULT 0)")), "legacy schema");
        expect(query.exec(QStringLiteral("INSERT INTO playlists(id,name) VALUES (42,'我喜欢的')")), "legacy same-name playlist");
    }
    expect(database.open(dbPath, QStringLiteral("selftest"), &error), "db open");
    const auto favorites = database.favoritesPlaylistId();
    expect(favorites > 0 && favorites != 42, "system favorite has independent identity");
    expect(database.playlists().first().isFavorites(), "system favorite ordered first");
    expect(database.playlists().size() == 2, "same-name ordinary playlist preserved");
    expect(database.open(dbPath, QStringLiteral("selftest"), &error), "db reopened");
    expect(database.favoritesPlaylistId() == favorites && database.playlists().size() == 2, "idempotent migration");
    echo::Database fresh;
    expect(fresh.open(dir.filePath(QStringLiteral("fresh.sqlite")), QStringLiteral("fresh"), &error), "fresh db open");
    expect(fresh.playlists().size() == 1 && fresh.playlists().first().isFavorites(), "fresh default favorite");

    const qint64 root = database.addRoot(dir.path());
    echo::Scanner scanner(dbPath, dir.filePath(QStringLiteral("art")));
    scanner.scanRoot(root, dir.path());
    const auto tracks = database.queryTracks(QStringLiteral("WHERE missing=0"), {}, QStringLiteral("ORDER BY title"));
    bool sawCue = false;
    bool sawWav = false;
    for (const auto& track : tracks) {
        if (track.title == QLatin1String("First")) {
            sawCue = true;
        }
        if (track.path.endsWith(QStringLiteral("tone.wav"))) {
            sawWav = true;
        }
    }
    expect(sawCue, "cue indexed");
    expect(sawWav, "wav indexed");
    const auto found = database.searchTracks(QStringLiteral("\"First\"*"), 10);
    expect(!found.isEmpty(), "fts");
    const qint64 first = tracks[0].id;
    const qint64 second = tracks[1].id;
    expect(database.setFavorites({first, first}, true, &error), "favorite insert");
    expect(database.playlistItems(favorites) == QVector<qint64>{first}, "favorite deduplicated");
    expect(database.setFavorites({second}, true, &error), "favorite second");
    expect(database.setFavorites({first}, true, &error), "favorite existing no reorder");
    expect(database.playlistItems(favorites) == QVector<qint64>({second, first}), "recent favorites first");
    expect(database.setFavorites({first}, false, &error) && database.setFavorites({first}, true, &error), "unlike and relike");
    const QVector<qint64> favoriteOrder {first, second};
    expect(database.playlistItems(favorites) == favoriteOrder, "relike moves first");
    database.renamePlaylist(favorites, QStringLiteral("changed"));
    database.movePlaylist(favorites, database.createPlaylistFolder(QStringLiteral("folder"), 0));
    database.deletePlaylist(favorites);
    database.setPlaylistItems(favorites, {second});
    database.insertPlaylistItems(favorites, 0, {second});
    database.removePlaylistItems(favorites, {0});
    expect(database.playlists().first().name == QStringLiteral("我喜欢的") && database.playlists().first().folderId == 0,
           "favorite metadata protected");
    expect(database.playlistItems(favorites) == favoriteOrder, "generic mutation protected");
    database.setPlaylistItems(42, {first, first});
    expect(database.playlistItems(42).size() == 2, "ordinary duplicates retained");
    database.renamePlaylist(42, QStringLiteral("ordinary"));
    expect(database.playlists().last().name == QLatin1String("ordinary"), "ordinary playlist editable");
    QSqlQuery favoriteSql(QSqlDatabase::database(QStringLiteral("selftest")));
    expect(favoriteSql.exec(QStringLiteral("CREATE TRIGGER reject_favorite BEFORE INSERT ON playlist_items WHEN new.playlist_id=%1 BEGIN SELECT RAISE(ABORT, 'injected write failure'); END").arg(favorites)), "failure injection");
    expect(!database.setFavorites({first}, false, &error) && !error.isEmpty(), "write failure surfaced");
    expect(database.playlistItems(favorites) == favoriteOrder, "write failure rolls back deletion");
    favoriteSql.exec(QStringLiteral("DROP TRIGGER reject_favorite"));
    expect(!favoriteSql.exec(QStringLiteral("DELETE FROM playlists WHERE id=%1").arg(favorites)), "SQL delete protected");
    expect(!favoriteSql.exec(QStringLiteral("UPDATE playlists SET name='bad' WHERE id=%1").arg(favorites)), "SQL rename protected");
    echo::Database reopened;
    expect(reopened.open(dbPath, QStringLiteral("reopened"), &error) && reopened.playlistItems(favorites) == favoriteOrder,
           "favorite persisted across connections");
    // A temporarily absent file preserves its favorite; all real DELETE paths clean up.
    echo::Track disposable = tracks[0];
    disposable.path = dir.filePath(QStringLiteral("disposable.wav"));
    disposable.rootId = database.addRoot(dir.filePath(QStringLiteral("other-root")));
    disposable.cueStartMs = -1;
    const qint64 disposableId = database.upsertTrack(disposable);
    database.setFavorites({disposableId}, true, &error);
    database.markRootMissing(disposable.rootId);
    expect(database.playlistItems(favorites).contains(disposableId), "missing file keeps favorite");
    database.removeRoot(disposable.rootId);
    expect(database.playlistItems(favorites).contains(disposableId) && !database.root(disposable.rootId).active,
        "source removal retains favorites");
    disposable.rootId = root;
    disposable.cueStartMs = 500;
    const qint64 cueId = database.upsertTrack(disposable);
    disposable.cueStartMs = -1;
    const qint64 wholeId = database.upsertTrack(disposable);
    database.setFavorites({cueId, wholeId}, true, &error);
    expect(favoriteSql.exec(QStringLiteral("UPDATE tracks SET missing=1 WHERE id=%1").arg(cueId)), "mark stale cue");
    expect(database.playlistItems(favorites).contains(cueId) && database.playlistItems(favorites).contains(wholeId),
        "stale cue references retained");
    database.removeTracks({ wholeId, cueId, disposableId });
    expect(database.playlistItems(favorites) == favoriteOrder, "track deletion cleans favorite");


    echo::SampleFormat pcm16;
    pcm16.sampleRate = 44100;
    pcm16.channels = 2;
    pcm16.bitsPerSample = 16;
    pcm16.bytesPerSample = 2;
    echo::DeviceFormat same16;
    same16.sampleRate = 44100;
    same16.channels = 2;
    same16.bitsPerSample = 16;
    same16.containerBytes = 2;
    const int16_t identityIn[] = {1, -2, 30000, -30000};
    int16_t identityOut[4] = {};
    uint32_t dither = 1;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(identityIn), pcm16, 2, reinterpret_cast<uint8_t*>(identityOut), same16, 1.f,
                         &dither);
    expect(std::memcmp(identityIn, identityOut, sizeof(identityIn)) == 0, "16-bit passthrough");

    echo::SampleFormat pcm24;
    pcm24.sampleRate = 96000;
    pcm24.channels = 1;
    pcm24.bitsPerSample = 24;
    pcm24.bytesPerSample = 4;
    echo::DeviceFormat aligned24;
    aligned24.sampleRate = 96000;
    aligned24.channels = 1;
    aligned24.bitsPerSample = 24;
    aligned24.containerBytes = 4;
    aligned24.highAligned = true;
    const int32_t low24 = 0x00123456;
    int32_t high24 = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&low24), pcm24, 1, reinterpret_cast<uint8_t*>(&high24), aligned24, 1.f, &dither);
    expect(high24 == low24 * 256, "low-aligned 24-bit input is shifted into high-aligned output");
    pcm24.highAligned = true;
    int32_t preserved24 = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&high24), pcm24, 1,
        reinterpret_cast<uint8_t*>(&preserved24), aligned24, 1.f, &dither);
    expect(preserved24 == high24, "high-aligned 24-bit passthrough preserves every bit");
    echo::DeviceFormat lowAligned24 = aligned24; lowAligned24.highAligned = false;
    int32_t roundTrip24 = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&high24), pcm24, 1,
        reinterpret_cast<uint8_t*>(&roundTrip24), lowAligned24, 1.f, &dither);
    expect(roundTrip24 == low24, "high-aligned to low-aligned 24-bit round trip");
    const int32_t negativeHigh = -low24 * 256;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&negativeHigh), pcm24, 1,
        reinterpret_cast<uint8_t*>(&roundTrip24), lowAligned24, 1.f, &dither);
    expect(roundTrip24 == -low24, "24-bit alignment conversion retains negative amplitude");

    echo::SampleFormat ffmpeg24;
    ffmpeg24.sampleRate = 48000;
    ffmpeg24.channels = 1;
    ffmpeg24.bitsPerSample = 24;
    ffmpeg24.bytesPerSample = 4;
    ffmpeg24.highAligned = true;
    const int32_t shifted = 3 << 8;
    echo::DeviceFormat floatOut;
    floatOut.sampleRate = 48000;
    floatOut.channels = 1;
    floatOut.bitsPerSample = 32;
    floatOut.containerBytes = 4;
    floatOut.isFloat = true;
    float decoded24 = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&shifted), ffmpeg24, 1, reinterpret_cast<uint8_t*>(&decoded24), floatOut,
                         1.f, &dither);
    expect(std::fabs(decoded24 - (3.f / 8388608.f)) < 1e-8f, "24-bit high align to float");
    const int32_t fullScale = 0x7FFFFF << 8;
    float fullFloat = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&fullScale), ffmpeg24, 1, reinterpret_cast<uint8_t*>(&fullFloat), floatOut,
                         1.f, &dither);
    expect(std::fabs(fullFloat - (8388607.f / 8388608.f)) < 1e-6f, "24-bit full scale");

    echo::SampleFormat surround;
    surround.sampleRate = 48000; surround.channels = 6; surround.bitsPerSample = 64;
    surround.bytesPerSample = 8; surround.isFloat = true; surround.channelMask = 0x3f;
    echo::DeviceFormat stereo {48000, 2, 64, 8, true};
    const double center[] {0, 0, 0.5, 0, 0, 0};
    double mixed[2] {};
    echo::convertSamples(reinterpret_cast<const uint8_t*>(center), surround, 1,
        reinterpret_cast<uint8_t*>(mixed), stereo, 1, nullptr);
    expect(mixed[0] > 0.1 && mixed[0] == mixed[1], "5.1 center is mixed into both stereo channels");
    const double lfeOnly[] {0, 0, 0, 0.5, 0, 0};
    echo::convertSamples(reinterpret_cast<const uint8_t*>(lfeOnly), surround, 1,
        reinterpret_cast<uint8_t*>(mixed), stereo, 1, nullptr);
    expect(mixed[0] == 0 && mixed[1] == 0, "stereo downmix excludes LFE by default");
    echo::SampleFormat precise = surround; precise.channels = 1; precise.channelMask = 0;
    echo::DeviceFormat doubleOut {48000, 1, 64, 8, true};
    const double fine = 0.123456789123456;
    double gained = 0;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(&fine), precise, 1,
        reinterpret_cast<uint8_t*>(&gained), doubleOut, 0.5f, nullptr);
    expect(std::abs(gained - fine * .5) < 1e-15, "gain retains double precision");
    echo::DeviceFormat packed24 {48000, 1, 24, 3, false};
    std::vector<double> lowLevel(32768, 0.25 / 8388608.);
    std::vector<uint8_t> quantized(lowLevel.size() * 3);
    uint32_t noiseState = 123;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(lowLevel.data()), precise, lowLevel.size(),
        quantized.data(), packed24, 1.f, &noiseState);
    double average = 0; bool varied = false;
    for (int i = 0; i < int(lowLevel.size()); ++i) {
        int32_t word = quantized[i*3] | (quantized[i*3+1] << 8) | (quantized[i*3+2] << 16);
        if (word & 0x800000) word |= static_cast<int32_t>(0xff000000);
        average += word; varied |= word != 0;
    }
    average /= lowLevel.size();
    expect(varied && std::abs(average - .25) < .02 && noiseState != 123, "24-bit TPDF preserves sub-LSB mean without truncation bias");
    echo::DeviceFormat reduced16 {48000, 1, 16, 2, false};
    echo::SampleFormat signed32 = precise; signed32.isFloat = false; signed32.bitsPerSample = 32; signed32.bytesPerSample = 4;
    const int32_t quiet32[] {12345, -12345, 45678, -45678};
    int16_t reduced[4] {};
    noiseState = 123;
    echo::convertSamples(reinterpret_cast<const uint8_t*>(quiet32), signed32, 4,
        reinterpret_cast<uint8_t*>(reduced), reduced16, 1.f, &noiseState);
    expect(noiseState != 123, "integer bit-depth reduction is dithered");

    const QString wav24 = dir.filePath(QStringLiteral("tone24.wav"));
    ffmpeg({QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("sine=frequency=440:duration=0.2:sample_rate=48000"), QStringLiteral("-c:a"),
            QStringLiteral("pcm_s24le"), wav24});
    echo::Decoder wav24Decoder;
    expect(wav24Decoder.open(wav24, &error), "24-bit wav open");
    expect(wav24Decoder.format().sampleRate == 48000 && wav24Decoder.format().bitsPerSample == 24 && wav24Decoder.format().highAligned,
           "24-bit wav format");
    std::vector<uint8_t> wav24Pcm(static_cast<size_t>(2048 * std::max(1, wav24Decoder.format().bytesPerFrame())));
    const int wav24Frames = wav24Decoder.read(wav24Pcm.data(), 2048);
    expect(wav24Frames > 0, "24-bit wav read");
    std::vector<float> wav24Float(static_cast<size_t>(std::max(wav24Frames, 0)));
    if (wav24Frames > 0) {
        echo::convertSamples(wav24Pcm.data(), wav24Decoder.format(), wav24Frames, reinterpret_cast<uint8_t*>(wav24Float.data()),
                             floatOut, 1.f, &dither);
        float peak = 0;
        for (float sample : wav24Float) {
            peak = std::max(peak, std::fabs(sample));
        }
        expect(std::fabs(peak - 0.124969f) < 0.002f, "24-bit wav peak");
    }

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
