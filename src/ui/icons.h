#pragma once

#include <QColor>
#include <QIcon>

namespace echo {

enum class Icon {
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
    Lyrics,
    Plus,
    Shuffle,
    Repeat
};

[[nodiscard]] QIcon icon(Icon kind, const QColor& color, int size = 16);

} // namespace echo
