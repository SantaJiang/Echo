#include "ui/dialogs.h"
#include "ui/settingsdialog.h"
#include "playback/player.h"
#include "ui/theme.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

namespace echo {
SettingsDialog::SettingsDialog(Player* player, QWidget* parent)
    : DialogShell(parent), m_player(player)
{
    setWindowTitle(QStringLiteral("Echo 设置"));
    setObjectName(QStringLiteral("settingsDialog"));
    setFixedWidth(544);

    styleDialog(this);
    auto* layout = contentLayout();
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(14);
    auto* heading = new QLabel(QStringLiteral("设置"), this);
    heading->setObjectName(QStringLiteral("dialogTitle"));
    m_normalize = new QCheckBox(QStringLiteral("音量均衡"), this);
    m_normalize->setObjectName(QStringLiteral("normalizationToggle"));
    auto* description = new QLabel(QStringLiteral("自动分析当前播放队列，让不同歌曲的听感音量更接近。保持歌曲内部的动态变化，不修改音乐文件。"), this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: #98989D;"));
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #98989D; font-size: 12px;"));
    layout->addWidget(heading);
    m_replaceQueue = new QCheckBox(QStringLiteral("双击歌曲时替换当前播放队列"), this);
    m_replaceQueue->setObjectName(QStringLiteral("replaceContextQueueToggle"));
    auto* queueDescription
        = new QLabel(QStringLiteral("适用于文件夹、播放列表和「我喜欢的」。关闭后，仅插入并立即播放所选歌曲，播完接回原队列。"), this);
    queueDescription->setWordWrap(true);
    queueDescription->setStyleSheet(QStringLiteral("color: #98989D;"));
    layout->addWidget(m_replaceQueue);
    layout->addWidget(queueDescription);
    connect(m_replaceQueue, &QCheckBox::toggled, player, &Player::setReplaceContextQueue);
    layout->addSpacing(12);
    layout->addWidget(m_normalize);
    layout->addWidget(description);
    layout->addWidget(m_status);
#ifdef Q_OS_MACOS
    layout->addSpacing(12);
    m_spatial = new QCheckBox(QStringLiteral("AirPods 空间音频"), this);
    m_spatial->setObjectName(QStringLiteral("spatialAudioToggle"));
    auto* spatialDescription = new QLabel(QStringLiteral("连接支持空间音频的 AirPods，播放音乐后在控制中心的声音面板选择「固定」或「头部跟踪」。此功能不能与独占输出同时使用。"), this);
    spatialDescription->setWordWrap(true);
    spatialDescription->setStyleSheet(QStringLiteral("color: #98989D;"));
    m_spatialStatus = new QLabel(this);
    m_spatialStatus->setObjectName(QStringLiteral("spatialAudioStatus"));
    m_spatialStatus->setWordWrap(true);
    m_spatialStatus->setMinimumHeight(54);
    m_spatialStatus->setStyleSheet(QStringLiteral("color: #98989D; font-size: 12px;"));
    layout->addWidget(m_spatial);
    layout->addWidget(spatialDescription);
    layout->addWidget(m_spatialStatus);
    connect(m_spatial, &QCheckBox::toggled, player, &Player::setSpatialEnabled);
    connect(player, &Player::spatialChanged, this, &SettingsDialog::refresh);

#endif
    auto* actions = new QDialogButtonBox(this);
    auto* done = actions->addButton(QStringLiteral("完成"), QDialogButtonBox::AcceptRole);
    done->setDefault(true);
    done->setProperty("primary", true);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_normalize, &QCheckBox::toggled, player, &Player::setNormalizationEnabled);
    connect(player, &Player::normalizationChanged, this, &SettingsDialog::refresh);
    connect(player, &Player::changed, this, &SettingsDialog::refresh);
    refresh();
}

void SettingsDialog::refresh()
{
    m_replaceQueue->blockSignals(true);
    m_replaceQueue->setChecked(m_player->replaceContextQueue());
    m_replaceQueue->blockSignals(false);
    m_normalize->blockSignals(true);
    m_normalize->setChecked(m_player->normalizationEnabled());
    m_normalize->blockSignals(false);
    m_status->setText(m_player->normalizationStatus());
    if (m_spatial) {
        m_spatial->blockSignals(true);
        m_spatial->setChecked(m_player->spatialEnabled());
        m_spatial->blockSignals(false);
        m_spatialStatus->setText(m_player->spatialStatus());
    }
}
}
