#include "library/pagequery.h"

#include "library/database.h"
#include "library/library.h"

#include <QStringList>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace echo {
namespace {
QString indexedOrder(int sort, bool descending)
{
    const QStringList columns { QStringLiteral("track_number"), QStringLiteral("title"), QStringLiteral("artist"),
        QStringLiteral("album"), QStringLiteral("duration_ms") };
    return Library::orderSql(columns.value(sort, QStringLiteral("title")), descending);
}
}

LibraryPage queryLibraryPage(const QString& databasePath, const QString& kind, const QString& key, const QString& search,
    int sort, bool descending)
{
    LibraryPage result;
    if (kind == QLatin1String("search")) {
        const auto found = Library::searchDatabase(databasePath, search);
        result.tracks = found.tracks;
        result.albums = found.albums;
        result.artists = found.artists;
        result.playlists = found.playlists;
        return result;
    }
    const QString connection = QStringLiteral("page-") + QUuid::createUuid().toString(QUuid::Id128);
    const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    Database db;
    if (!db.open(databasePath, connection, &result.error, true)) {
        return result;
    }
    QSqlQuery transaction(QSqlDatabase::database(connection));
    transaction.exec(QStringLiteral("BEGIN"));
    const QString scope = QStringLiteral("WHERE ") + Database::libraryScope();
    const QString order = indexedOrder(sort, descending);
    if (kind == QLatin1String("songs")) {
        result.tracks = db.queryTracks(scope, {}, order);
    } else if (kind == QLatin1String("album")) {
        result.tracks = db.queryTracks(scope + QStringLiteral(" AND album_key=?"), { key },
            sort ? order : QStringLiteral("ORDER BY disc_number,track_number,title,id"));
    } else if (kind == QLatin1String("artist")) {
        result.albums = db.albums(key);
        result.tracks = db.queryTracks(scope + QStringLiteral(" AND artist_key=?"), { key },
            sort ? order : QStringLiteral("ORDER BY album,disc_number,track_number,id"));
    } else if (kind == QLatin1String("source")) {
        result.tracks = db.queryTracks(QStringLiteral("WHERE root_id=?"), { key.toLongLong() },
            sort ? order : QStringLiteral("ORDER BY album,disc_number,track_number,title,id"));
    } else if (kind == QLatin1String("playlist")) {
        QVector<qint64> ids;
        QVector<qint64> entryIds;
        for (const auto& entry : db.playlistEntries(key.toLongLong())) {
            entryIds << entry.entryId;
            ids << entry.trackId;
        }
        result.tracks = db.tracksByIds(ids);
        for (int i = 0; i < result.tracks.size(); ++i) {
            result.tracks[i].listEntryId = entryIds.value(i);
            result.tracks[i].playlistPosition = i;
        }
        if (sort) {
            std::stable_sort(result.tracks.begin(), result.tracks.end(), [=](const Track& a, const Track& b) {
                const int compared = sort == 4 ? (a.durationMs > b.durationMs ? 1 : a.durationMs < b.durationMs ? -1 : 0)
                                                : QString::localeAwareCompare(sort == 2 ? a.displayArtist()
                                                          : sort == 3         ? a.displayAlbum()
                                                                              : a.displayTitle(),
                                                      sort == 2       ? b.displayArtist()
                                                          : sort == 3 ? b.displayAlbum()
                                                                      : b.displayTitle());
                return descending ? compared > 0 : compared < 0;
            });
        }
    } else if (kind == QLatin1String("albums")) {
        result.albums = db.albums({});
    } else if (kind == QLatin1String("artists")) {
        result.artists = db.artists();
    } else if (kind == QLatin1String("sources")) {
        result.roots = db.roots();
    }
    transaction.exec(QStringLiteral("COMMIT"));
    return result;
}

} // namespace echo
