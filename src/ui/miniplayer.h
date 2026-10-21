#pragma once

#include "library/types.h"

#include <QWidget>
#include <QPixmap>

class QLabel;
class QToolButton;
class QSlider;

namespace echo {

class MiniPlayer : public QWidget {
    Q_OBJECT

public:
    explicit MiniPlayer(QWidget* parent = nullptr);
    void setFavorite(bool liked, bool enabled);
    void setPosition(qint64 positionMs, qint64 durationMs, bool seekable = true);
    void setTrack(const Track& track, const QPixmap& artwork, bool playing);

signals:
    void seekRequested(qint64 positionMs);
    void favoriteRequested();
    void toggleRequested();
    void nextRequested();
    void previousRequested();
    void expandRequested();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QSlider* m_seek = nullptr;
    qint64 m_trackId = 0;
    bool m_canSeek = false;
    Track m_lastTrack;
    qint64 m_artworkKey = -1;
    qreal m_artworkDpr = 0;
    QPixmap m_lastArtwork;
    bool m_lastPlaying = false;
    bool m_initialized = false;
    QPoint m_dragOffset;
    bool m_dragging = false;
    QLabel* m_title = nullptr;
    QLabel* m_artist = nullptr;
    QLabel* m_art = nullptr;
    QToolButton* m_play = nullptr;
    QToolButton* m_favorite = nullptr;
};

} // namespace echo
