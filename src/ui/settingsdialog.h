#pragma once
#include "ui/dialogs.h"

class QCheckBox;
class QLabel;

namespace echo {
class Player;
class SettingsDialog : public DialogShell {
    Q_OBJECT
public:
    explicit SettingsDialog(Player* player, QWidget* parent = nullptr);
private:
    void refresh();
    Player* m_player;
    QCheckBox* m_replaceQueue;
    QCheckBox* m_normalize;
    QLabel* m_status;
    QCheckBox* m_spatial = nullptr;
    QLabel* m_spatialStatus = nullptr;
};
}
