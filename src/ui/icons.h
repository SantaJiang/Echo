#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>


namespace echo {

enum class Icon {
    Heart,
    HeartFilled,
    Songs,
    Albums,
    Artists,
    Folders,
    Playlist,
    Search,
    Play,
    Pause,
    Next,
    Previous,
    Volume,
    Queue,
    Plus,
    Shuffle,
    Repeat,
    Expand,
    Settings
};

[[nodiscard]] QIcon icon(Icon kind, const QColor& color, int size = 16);
[[nodiscard]] QPixmap placeholderCover(int side);

} // namespace echo
