#include "audio/cue.h"
#include "audio/decoder.h"
#include "library/database.h"
#include "library/scanner.h"
#include "library/types.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

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
    cueFile.open(QIODevice::WriteOnly);
    cueFile.write("PERFORMER \"Tester\"\nTITLE \"Tones\"\nFILE \"tone.flac\" WAVE\n"
                  "  TRACK 01 AUDIO\n    TITLE \"First\"\n    INDEX 01 00:00:00\n"
                  "  TRACK 02 AUDIO\n    TITLE \"Second\"\n    INDEX 01 00:00:40\n");
    cueFile.close();

    echo::Database database;
    const QString dbPath = dir.filePath(QStringLiteral("library.sqlite"));
    expect(database.open(dbPath, QStringLiteral("selftest"), &error), "db open");
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

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
