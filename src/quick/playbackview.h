#pragma once

#include "library/types.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace echo {
class Library;
class Player;
class AudioEngine;

// Presentation only: all PCM processing and playback remain in echo_core.
class PlaybackView : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap track READ track NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY trackChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY playbackChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY playbackChanged)
    Q_PROPERTY(int repeat READ repeat NOTIFY playbackChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY positionChanged)
public:
    PlaybackView(Library* library, Player* player, AudioEngine* engine, QObject* parent = nullptr);
    QVariantMap track() const;
    bool playing() const;
    qint64 position() const;
    qint64 duration() const { return m_track.durationMs; }
    float volume() const;
    bool shuffle() const;
    int repeat() const;
    QVariantList lyrics() const { return m_lyrics; }
    int lyricIndex() const;
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 milliseconds);
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    void setVolume(float value);
    Q_INVOKABLE void setPresentationActive(bool active);
signals:
    void trackChanged();
    void lyricsChanged();
    void playbackChanged();
    void positionChanged();

private:
    void refresh(bool reloadMetadata = false);
    Library* m_library;
    Player* m_player;
    AudioEngine* m_engine;
    Track m_track;
    QVariantList m_lyrics;
    bool m_presentationActive = true;
    QTimer m_clock;
};
}
