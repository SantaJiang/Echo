#include "audio/convert.h"
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
    expect(high24 == low24, "24-bit word preserved");

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
