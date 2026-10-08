#pragma once

#include "library/types.h"

namespace echo {

struct ScannedTags {
    Track track;
    QByteArray artwork;
    QString cuesheet;
    bool ok = false;
};

[[nodiscard]] ScannedTags readTags(const QString& path);
[[nodiscard]] bool writeTags(const QString& path, const TagEdit& edit, QString* error);
[[nodiscard]] bool rewriteCueEntry(const QString& cuePath, int trackNumber, const QString& title, const QString& performer,
                                   QString* error);

} // namespace echo
