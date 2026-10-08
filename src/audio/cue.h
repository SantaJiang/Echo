#pragma once

#include <QString>
#include <QVector>

namespace echo {

struct CueTrack {
    int number = 0;
    QString title;
    QString performer;
    QString file;
    qint64 startMs = 0;
    qint64 endMs = -1;
    bool hasTrackGain = false;
    double trackGainDb = 0;
    bool hasAlbumGain = false;
    double albumGainDb = 0;
};

// Parse a CUE sheet. Track end times are the next INDEX 01, or -1 for the last track of each file.
QVector<CueTrack> parseCueSheet(const QString& text, QString* error = nullptr);

} // namespace echo
