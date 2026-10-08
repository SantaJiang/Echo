#include "audio/cue.h"

#include <QRegularExpression>

namespace echo {
namespace {

qint64 indexToMs(const QString& token)
{
    const auto parts = token.split(u':');
    if (parts.size() != 3) {
        return -1;
    }
    bool okMin = false;
    bool okSec = false;
    bool okFrame = false;
    const int minutes = parts[0].toInt(&okMin);
    const int seconds = parts[1].toInt(&okSec);
    const int frames = parts[2].toInt(&okFrame);
    if (!okMin || !okSec || !okFrame || seconds < 0 || seconds >= 60 || frames < 0 || frames >= 75) {
        return -1;
    }
    return (static_cast<qint64>(minutes) * 60 + seconds) * 1000 + (static_cast<qint64>(frames) * 1000) / 75;
}

QString unquote(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.front() == u'"' && text.back() == u'"') {
        text = text.mid(1, text.size() - 2);
    }
    return text;
}

double parseGain(const QString& text)
{
    QString value = text;
    value.replace(QStringLiteral("dB"), QString(), Qt::CaseInsensitive);
    return value.trimmed().toDouble();
}

void closeFileGroup(QVector<CueTrack>& tracks, int groupStart)
{
    for (int i = groupStart; i < tracks.size(); ++i) {
        if (i + 1 < tracks.size() && tracks[i + 1].file == tracks[i].file) {
            tracks[i].endMs = tracks[i + 1].startMs;
        }
    }
}

} // namespace

QVector<CueTrack> parseCueSheet(const QString& text, QString* error)
{
    QVector<CueTrack> tracks;
    QString albumPerformer;
    QString albumTitle;
    bool hasAlbumGain = false;
    double albumGain = 0;
    QString currentFile;
    int fileGroupStart = 0;
    CueTrack current;
    bool inTrack = false;
    bool sawIndex = false;

    const auto lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';')) {
            continue;
        }
        const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }
        const QString key = parts[0].toUpper();
        if (key == QLatin1String("REM") && parts.size() >= 3) {
            const QString rem = parts[1].toUpper();
            const QString rest = line.mid(line.indexOf(parts[1]) + parts[1].size()).trimmed();
            if (rem == QLatin1String("REPLAYGAIN_ALBUM_GAIN")) {
                hasAlbumGain = true;
                albumGain = parseGain(rest);
            } else if (rem == QLatin1String("REPLAYGAIN_TRACK_GAIN") && inTrack) {
                current.hasTrackGain = true;
                current.trackGainDb = parseGain(rest);
            }
            continue;
        }
        if (key == QLatin1String("PERFORMER") && !inTrack) {
            albumPerformer = unquote(line.mid(QStringLiteral("PERFORMER").size()));
            continue;
        }
        if (key == QLatin1String("TITLE") && !inTrack) {
            albumTitle = unquote(line.mid(QStringLiteral("TITLE").size()));
            continue;
        }
        if (key == QLatin1String("FILE")) {
            if (inTrack) {
                if (!sawIndex && error) {
                    *error = QStringLiteral("CUE 曲目缺少 INDEX");
                }
                tracks.push_back(current);
                inTrack = false;
            }
            closeFileGroup(tracks, fileGroupStart);
            fileGroupStart = tracks.size();
            QString payload = line.mid(QStringLiteral("FILE").size()).trimmed();
            if (payload.startsWith(u'"')) {
                const int end = payload.indexOf(u'"', 1);
                currentFile = end > 1 ? payload.mid(1, end - 1) : unquote(payload);
            } else {
                currentFile = payload.section(QLatin1Char(' '), 0, 0);
            }
            continue;
        }
        if (key == QLatin1String("TRACK")) {
            if (inTrack) {
                tracks.push_back(current);
            }
            current = CueTrack {};
            current.file = currentFile;
            current.performer = albumPerformer;
            current.title = albumTitle;
            current.hasAlbumGain = hasAlbumGain;
            current.albumGainDb = albumGain;
            current.number = parts.size() >= 2 ? parts[1].toInt() : tracks.size() + 1;
            inTrack = true;
            sawIndex = false;
            continue;
        }
        if (!inTrack) {
            continue;
        }
        if (key == QLatin1String("TITLE")) {
            current.title = unquote(line.mid(QStringLiteral("TITLE").size()));
        } else if (key == QLatin1String("PERFORMER")) {
            current.performer = unquote(line.mid(QStringLiteral("PERFORMER").size()));
        } else if (key == QLatin1String("INDEX") && parts.size() >= 3) {
            const int indexNumber = parts[1].toInt();
            const qint64 ms = indexToMs(parts[2]);
            if (ms < 0) {
                if (error) {
                    *error = QStringLiteral("无法解析 CUE 时间 ") + parts[2];
                }
                return {};
            }
            if (indexNumber == 1 || (indexNumber == 0 && !sawIndex)) {
                current.startMs = ms;
                sawIndex = true;
            }
        }
    }
    if (inTrack) {
        tracks.push_back(current);
    }
    closeFileGroup(tracks, fileGroupStart);
    if (tracks.isEmpty() && error && error->isEmpty()) {
        *error = QStringLiteral("CUE 里没有曲目");
    }
    return tracks;
}

} // namespace echo
