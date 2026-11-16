#include "audio/cue.h"
#include "audio/decoder.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <vector>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        QTextStream(stderr) << "用法: echo-probe <音频或 cue 文件>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (path.endsWith(QStringLiteral(".cue"), Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return 1;
        }
        QString error;
        const auto tracks = echo::parseCueSheet(QString::fromUtf8(file.readAll()), &error);
        if (!error.isEmpty()) {
            QTextStream(stderr) << error << "\n";
            return 1;
        }
        for (const auto& track : tracks) {
            QTextStream(stdout) << track.number << "\t" << track.startMs << "\t" << track.endMs << "\t" << track.title << "\t"
                                << track.file << "\n";
        }
        return tracks.isEmpty() ? 1 : 0;
    }
    echo::Decoder decoder;
    QString error;
    if (!decoder.open(path, &error)) {
        QTextStream(stderr) << error << "\n";
        return 1;
    }
    const auto& format = decoder.format();
    QTextStream(stdout) << "rate=" << format.sampleRate << " channels=" << format.channels << " bits=" << format.bitsPerSample
                        << " float=" << format.isFloat << " delay=" << decoder.encoderDelay()
                        << " padding=" << decoder.encoderPadding() << " duration_ms=" << decoder.durationMs() << "\n";
    const auto& gain = decoder.replayGain();
    if (gain.hasTrack || gain.hasAlbum) {
        QTextStream(stdout) << "track_gain=" << gain.trackDb << " album_gain=" << gain.albumDb << "\n";
    }
    std::vector<uint8_t> buffer(static_cast<size_t>(4096 * format.bytesPerFrame()));
    qint64 frames = 0;
    while (true) {
        const int read = decoder.read(buffer.data(), 4096);
        if (read < 0) {
            QTextStream(stderr) << "decode error\n";
            return 1;
        }
        if (read == 0) {
            break;
        }
        frames += read;
    }
    QTextStream(stdout) << "frames=" << frames << "\n";
    return 0;
}
