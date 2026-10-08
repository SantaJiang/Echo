#pragma once

#include "library/types.h"

#include <QWidget>

class QLabel;
class QToolButton;

namespace echo {

class MiniPlayer : public QWidget {
    Q_OBJECT

public:
    explicit MiniPlayer(QWidget* parent = nullptr);
    void setTrack(const Track& track, const QPixmap& artwork, bool playing);

signals:
    void toggleRequested();
    void nextRequested();
    void previousRequested();
    void expandRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLabel* m_title = nullptr;
    QLabel* m_artist = nullptr;
    QLabel* m_art = nullptr;
    QToolButton* m_play = nullptr;
};

} // namespace echo
