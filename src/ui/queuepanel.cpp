#include "ui/queuepanel.h"

#include "library/library.h"
#include "ui/theme.h"

#include <QAbstractItemModel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace echo {

QueuePanel::QueuePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("queuePanel"));
    setFixedWidth(320);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel(QStringLiteral("待播清单"), this));
    auto* clear = new QPushButton(QStringLiteral("清空"), this);
    clear->setFlat(true);
    header->addWidget(clear);
    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    layout->addLayout(header);
    layout->addWidget(m_list, 1);
    connect(clear, &QPushButton::clicked, this, &QueuePanel::cleared);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit activated(m_list->row(item));
    });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex&, int start, int, const QModelIndex&, int row) {
        if (m_filling) {
            return;
        }
        const int destination = row > start ? row - 1 : row;
        emit moved(start, destination);
    });
}

void QueuePanel::setQueue(const QVector<Player::Entry>& queue, int index, Library* library)
{
    m_filling = true;
    m_list->clear();
    for (int row = 0; row < queue.size(); ++row) {
        const Track track = library->track(queue[row].trackId);
        auto* item = new QListWidgetItem(track.displayTitle() + QStringLiteral(" — ") + track.displayArtist());
        if (row == index) {
            item->setForeground(echo::palette().accent);
        }
        m_list->addItem(item);
    }
    m_filling = false;
}

void QueuePanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit removed(m_list->currentRow());
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace echo
