#pragma once

#include "library/types.h"

#include <QWidget>

class QLabel;
class QListWidget;

namespace echo {

struct LyricLine {
    qint64 timeMs = -1;
    QString text;
};

[[nodiscard]] QVector<LyricLine> parseLyrics(const QString& text);

class NowPlayingPage : public QWidget {
    Q_OBJECT

public:
    explicit NowPlayingPage(QWidget* parent = nullptr);
    void setTrack(const Track& track, const QPixmap& artwork, qint64 positionMs);

signals:
    void editLyricsRequested();
    void artistRequested(const QString& artistKey);

private:
    QLabel* m_art = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_artist = nullptr;
    QWidget* m_lyricsPane = nullptr;
    QListWidget* m_lyrics = nullptr;
    QVector<LyricLine> m_lines;
};

} // namespace echo
