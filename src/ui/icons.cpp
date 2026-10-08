#include "ui/icons.h"

#include <QPainter>
#include <QPixmap>

#include <functional>

namespace echo {
namespace {

void stroke(QPainter& painter, const QColor& color, qreal width)
{
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
}

QIcon paint(int size, const std::function<void(QPainter&)>& draw)
{
    QPixmap pixmap(size * 2, size * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(1, 1);
    draw(painter);
    return QIcon(pixmap);
}

} // namespace

QIcon icon(Icon kind, const QColor& color, int size)
{
    return paint(size, [&](QPainter& painter) {
        const QRectF box(1, 1, size - 2, size - 2);
        switch (kind) {
        case Icon::Songs:
            stroke(painter, color, 1.4);
            painter.drawEllipse(QRectF(2, size - 7, 4.5, 4.5));
            painter.drawEllipse(QRectF(8, size - 8.5, 4.5, 4.5));
            painter.drawLine(QPointF(6.4, size - 4.6), QPointF(6.4, 3));
            painter.drawLine(QPointF(12.4, size - 6.2), QPointF(12.4, 4.2));
            painter.drawLine(QPointF(6.4, 3), QPointF(12.4, 4.2));
            break;
        case Icon::Albums:
            stroke(painter, color, 1.4);
            painter.drawRoundedRect(QRectF(2, 2, size - 4, size - 4), 2, 2);
            painter.drawEllipse(QRectF(size / 2.0 - 3, size / 2.0 - 3, 6, 6));
            break;
        case Icon::Artists:
            stroke(painter, color, 1.4);
            painter.drawEllipse(QRectF(size / 2.0 - 2.4, 2, 4.8, 4.8));
            painter.drawArc(QRectF(3, 8, size - 6, size), 0, 180 * 16);
            break;
        case Icon::Folders:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(1.5, 5, size - 3, size - 7), 1.6, 1.6);
            painter.drawRoundedRect(QRectF(1.5, 3, 6, 3), 1, 1);
            break;
        case Icon::Playlist:
            stroke(painter, color, 1.5);
            for (int row = 0; row < 3; ++row) {
                const qreal y = 4 + row * 4.2;
                painter.drawLine(QPointF(3, y), QPointF(size - 3, y));
            }
            break;
        case Icon::Search: {
            stroke(painter, color, 1.6);
            painter.drawEllipse(QRectF(2, 2, 8, 8));
            painter.drawLine(QPointF(9.2, 9.2), QPointF(size - 2, size - 2));
            break;
        }
        case Icon::Play:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF {{QPointF(4, 2), QPointF(4, size - 2), QPointF(size - 2, size / 2.0)}});
            break;
        case Icon::Pause:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(3, 2, 3.2, size - 4), 1, 1);
            painter.drawRoundedRect(QRectF(size - 6.2, 2, 3.2, size - 4), 1, 1);
            break;
        case Icon::Next:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF {{QPointF(2, 3), QPointF(2, size - 3), QPointF(size - 6, size / 2.0)}});
            painter.drawRoundedRect(QRectF(size - 4.5, 3, 2.2, size - 6), 0.6, 0.6);
            break;
        case Icon::Previous:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF {{QPointF(size - 2, 3), QPointF(size - 2, size - 3), QPointF(6, size / 2.0)}});
            painter.drawRoundedRect(QRectF(2.2, 3, 2.2, size - 6), 0.6, 0.6);
            break;
        case Icon::Volume:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(QRectF(2, 6, 3, 4));
            painter.drawPolygon(QPolygonF {{QPointF(5, 6), QPointF(9, 3), QPointF(9, size - 3), QPointF(5, 10)}});
            stroke(painter, color, 1.3);
            painter.drawArc(QRectF(8, 4, 6, 8), -40 * 16, -100 * 16);
            break;
        case Icon::Queue:
            stroke(painter, color, 1.4);
            for (int row = 0; row < 3; ++row) {
                painter.drawLine(QPointF(3, 4 + row * 4), QPointF(size - 5, 4 + row * 4));
            }
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QRectF(size - 5, size - 6, 3, 3));
            break;
        case Icon::Lyrics:
            stroke(painter, color, 1.3);
            painter.drawRoundedRect(box.adjusted(1, 0, -1, 0), 2, 2);
            painter.drawLine(QPointF(4, 5), QPointF(size - 4, 5));
            painter.drawLine(QPointF(4, 8), QPointF(size - 5, 8));
            painter.drawLine(QPointF(4, 11), QPointF(size - 6, 11));
            break;
        case Icon::Plus:
            stroke(painter, color, 1.6);
            painter.drawLine(QPointF(size / 2.0, 3), QPointF(size / 2.0, size - 3));
            painter.drawLine(QPointF(3, size / 2.0), QPointF(size - 3, size / 2.0));
            break;
        case Icon::Shuffle:
            stroke(painter, color, 1.4);
            painter.drawLine(QPointF(2, 4), QPointF(6, 4));
            painter.drawLine(QPointF(6, 4), QPointF(size - 3, size - 4));
            painter.drawLine(QPointF(2, size - 4), QPointF(6, size - 4));
            painter.drawLine(QPointF(6, size - 4), QPointF(size - 3, 4));
            break;
        case Icon::Repeat:
            stroke(painter, color, 1.4);
            painter.drawArc(QRectF(3, 3, size - 6, size - 6), 40 * 16, 280 * 16);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF {{QPointF(size - 4, 2), QPointF(size - 1, 5), QPointF(size - 6, 5)}});
            break;
        }
    });
}

} // namespace echo
