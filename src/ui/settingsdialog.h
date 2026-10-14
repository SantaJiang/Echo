#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;

namespace echo {
class Player;
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Player* player, QWidget* parent = nullptr);
private:
    void refresh();
    Player* m_player;
    QCheckBox* m_normalize;
    QLabel* m_status;
    QCheckBox* m_spatial = nullptr;
    QLabel* m_spatialStatus = nullptr;
};
}
