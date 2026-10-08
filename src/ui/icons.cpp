#include "ui/icons.h"

#include <algorithm>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace echo {
namespace {

struct GlyphFont {
    QString family;
    bool ready = false;
};

GlyphFont& glyphs()
{
    static GlyphFont font;
    if (!font.ready) {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/fa-solid-900.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        font.family = families.isEmpty() ? QStringLiteral("Font Awesome 6 Free") : families.constFirst();
        font.ready = true;
    }
    return font;
}

QChar codepoint(Icon kind)
{
    switch (kind) {
    case Icon::Songs:
        return QChar(0xf001);
    case Icon::Albums:
        return QChar(0xf51f);
    case Icon::Artists:
        return QChar(0xf007);
    case Icon::Folders:
        return QChar(0xf07b);
    case Icon::Playlist:
        return QChar(0xf03a);
    case Icon::Search:
        return QChar(0xf002);
    case Icon::Play:
        return QChar(0xf04b);
    case Icon::Pause:
        return QChar(0xf04c);
    case Icon::Next:
        return QChar(0xf051);
    case Icon::Previous:
        return QChar(0xf048);
    case Icon::Volume:
        return QChar(0xf028);
    case Icon::Queue:
        return QChar(0xf0ca);
    case Icon::Lyrics:
        return QChar(0xf075);
    case Icon::Plus:
        return QChar(0x002b);
    case Icon::Shuffle:
        return QChar(0xf074);
    case Icon::Repeat:
        return QChar(0xf363);
    case Icon::Expand:
        return QChar(0xf077);
    }
    return QChar(0xf001);
}

QFont iconFont(int size)
{
    QFont font(glyphs().family);
    font.setPixelSize(size);
    font.setWeight(QFont::Black);
    font.setStyleName(QStringLiteral("Solid"));
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

QIcon icon(Icon kind, const QColor& color, int size)
{
    const int side = std::max(size, 8);
    QPixmap pixmap(side * 2, side * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QString glyph(codepoint(kind));
    const QFont font = iconFont(std::max(8, side * 4 / 5));
    painter.setFont(font);
    painter.setPen(color);
    const QFontMetrics metrics(font);
    const QRect bounds = metrics.tightBoundingRect(glyph);
    const qreal x = (side - bounds.width()) / 2.0 - bounds.x();
    const qreal y = (side - bounds.height()) / 2.0 - bounds.y();
    painter.drawText(QPointF(x, y), glyph);
    return QIcon(pixmap);
}

QPixmap placeholderCover(int side)
{
    QPixmap pixmap(side * 2, side * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const qreal radius = std::max(6.0, side * 0.08);
    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, side - 1, side - 1), radius, radius);
    painter.fillPath(path, QColor(QStringLiteral("#3A3A3C")));
    painter.setPen(Qt::NoPen);
    const qreal disc = side * 0.34;
    const QPointF center(side / 2.0, side / 2.0);
    painter.setBrush(QColor(QStringLiteral("#636366")));
    painter.drawEllipse(center, disc, disc);
    painter.setBrush(QColor(QStringLiteral("#3A3A3C")));
    painter.drawEllipse(center, disc * 0.22, disc * 0.22);
    return pixmap;
}

} // namespace echo
