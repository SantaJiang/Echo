#pragma once

#include "library/types.h"

#include <QString>
#include <QVector>

namespace echo {

struct LibraryPage {
    QVector<Track> tracks;
    QVector<Album> albums;
    QVector<Artist> artists;
    QVector<Playlist> playlists;
    QVector<LibraryRoot> roots;
    QString error;
};

// Read-only page query. Sorting uses the same column whitelist as Library::orderSql.
[[nodiscard]] LibraryPage queryLibraryPage(const QString& databasePath, const QString& kind, const QString& key,
    const QString& search, int sort, bool descending);

} // namespace echo
