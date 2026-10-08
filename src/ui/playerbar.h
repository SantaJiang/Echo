#pragma once

#include "library/types.h"

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace echo {

class PlayerBar : public QWidget {
    Q_OBJECT

public:
    explicit PlayerBar(QWidget* parent = nullptr);
    void setTrack(const Track& track, const QPixmap& artwork);
    void setPosition(qint64 positionMs, qint64 durationMs);
    void setPlaying(bool playing);
    void setVolume(float volume);
    void setShuffle(bool enabled);
    void setRepeat(int mode);
    void setFormatText(const QString& text);
    void setStatus(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void toggleRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 positionMs);
    void volumeChanged(float volume);
    void shuffleToggled();
    void repeatToggled();
    void queueToggled();
    void lyricsRequested();
    void artworkClicked();

private:
    QLabel* m_art = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_artist = nullptr;
    QLabel* m_elapsed = nullptr;
    QLabel* m_duration = nullptr;
    QLabel* m_format = nullptr;
    QSlider* m_seek = nullptr;
    QSlider* m_volume = nullptr;
    QToolButton* m_play = nullptr;
    QToolButton* m_shuffle = nullptr;
    QToolButton* m_repeat = nullptr;
    bool m_seeking = false;
};

} // namespace echo
