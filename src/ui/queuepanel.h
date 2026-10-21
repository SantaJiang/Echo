#pragma once

#include "playback/player.h"

#include <QListWidget>
#include <QWidget>

namespace echo {

class QueuePanel : public QWidget {
    Q_OBJECT

public:
    explicit QueuePanel(QWidget* parent = nullptr);
    void setQueue(const QVector<Player::Entry>& queue, int index, Library* library);

protected:
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void activated(int index);
    void removed(int index);
    void moved(int from, int to);
    void cleared();

private:
    QListWidget* m_list = nullptr;
    bool m_filling = false;
    QVector<Player::Entry> m_queue;
    QVector<Track> m_tracks;
    int m_index = -1;
    quint64 m_revision = ~quint64(0);
};

} // namespace echo
