#include "library/trackrepository.h"

#include "library/metadata.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace echo {

Track TrackRepository::trackFromQuery(const QSqlQuery& query) const
{
    Track track;
    track.id = query.value(0).toLongLong();
    track.rootId = query.value(1).toLongLong();
    track.path = query.value(2).toString();
    track.folder = query.value(3).toString();
    track.title = query.value(4).toString();
    track.artist = query.value(5).toString();
    track.albumArtist = query.value(6).toString();
    track.album = query.value(7).toString();
    track.albumKey = query.value(8).toString();
    track.artistKey = query.value(9).toString();
    track.year = query.value(10).toInt();
    track.trackNumber = query.value(11).toInt();
    track.discNumber = query.value(12).toInt();
    track.durationMs = query.value(13).toLongLong();
    track.sampleRate = query.value(14).toInt();
    track.bitDepth = query.value(15).toInt();
    track.channels = query.value(16).toInt();
    track.codec = query.value(17).toString();
    track.hasTrackGain = query.value(18).toInt() != 0;
    track.hasAlbumGain = query.value(19).toInt() != 0;
    track.trackGainDb = query.value(20).toDouble();
    track.albumGainDb = query.value(21).toDouble();
    track.trackPeak = query.value(22).toDouble();
    track.albumPeak = query.value(23).toDouble();
    track.lyrics = query.value(24).toString();
    track.artworkHash = query.value(25).toString();
    track.mtime = query.value(26).toLongLong();
    track.fileSize = query.value(27).toLongLong();
    track.missing = query.value(28).toInt() != 0;
    track.cuePath = query.value(29).toString();
    track.cueNumber = query.value(30).toInt();
    track.cueStartMs = query.value(31).toLongLong();
    track.cueEndMs = query.value(32).isNull() ? -1 : query.value(32).toLongLong();
    return track;
}

Track TrackRepository::track(qint64 id) const
{
    const auto rows = tracksByIds({id});
    return rows.isEmpty() ? Track {} : rows.first();
}

QVector<Track> TrackRepository::tracksByIds(const QVector<qint64>& ids) const
{
    QVector<Track> result;
    QHash<qint64, Track> found;
    // Bounded IN batches avoid one SQL query per row while respecting SQLite bind limits.
    for (int offset = 0; offset < ids.size(); offset += 400) {
        QVariantList binds;
        QStringList placeholders;
        for (int i = offset; i < std::min(offset + 400, static_cast<int>(ids.size())); ++i) {
            binds << ids[i];
            placeholders << QStringLiteral("?");
        }
        for (const auto& track :
            queryTracks(QStringLiteral("WHERE id IN (") + placeholders.join(u',') + u')', binds, { }))
            found.insert(track.id, track);
    }
    result.reserve(ids.size());
    for (const auto id : ids)
        result.push_back(found.value(id));
    return result;
}

QVector<Track> TrackRepository::queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const
{
    QVector<Track> result;
    QSqlQuery query(database());
    query.prepare(
        QStringLiteral("SELECT id, root_id, path, folder, title, artist, album_artist, album, album_key, artist_key, "
                       "year, track_number, disc_number, duration_ms, sample_rate, bit_depth, channels, codec, "
                       "has_track_gain, has_album_gain, track_gain_db, album_gain_db, track_peak, album_peak, lyrics, "
                       "artwork_hash, mtime, file_size, CASE WHEN EXISTS (SELECT 1 FROM roots r WHERE "
                       "r.id=tracks.root_id AND r.active=1 AND r.status!='available') THEN 1 ELSE missing END, "
                       "cue_path, cue_number, cue_start_ms, cue_end_ms "
                       "FROM tracks ")
        + whereSql + QLatin1Char(' ') + orderSql);
    for (const QVariant& bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(trackFromQuery(query));
    }
    return result;
}

QVector<Album> TrackRepository::albums(const QString& artistKey) const
{
    QVector<Album> result;
    QSqlQuery query(database());
    QString sql = QStringLiteral(
        "SELECT album_key, MAX(album), MAX(album_artist), MAX(artist), MAX(year), COUNT(*), "
        "(SELECT artwork_hash FROM tracks t2 WHERE t2.album_key=tracks.album_key AND artwork_hash!='' LIMIT 1) "
        "FROM tracks WHERE %1 AND album_key!='' ")
                      .arg(Database::libraryScope());
    if (!artistKey.isEmpty()) {
        sql += QStringLiteral("AND artist_key=? ");
    }
    sql += QStringLiteral("GROUP BY album_key ORDER BY MAX(album) COLLATE NOCASE");
    query.prepare(sql);
    if (!artistKey.isEmpty()) {
        query.addBindValue(artistKey);
    }
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        Album album;
        album.key = query.value(0).toString();
        album.title = query.value(1).toString();
        const QString albumArtist = query.value(2).toString();
        album.artist = albumArtist.isEmpty() ? query.value(3).toString() : albumArtist;
        album.year = query.value(4).toInt();
        album.tracks = query.value(5).toInt();
        album.artworkHash = query.value(6).toString();
        if (album.title.isEmpty()) {
            album.title = QStringLiteral("未知专辑");
        }
        if (album.artist.isEmpty()) {
            album.artist = QStringLiteral("未知艺术家");
        }
        result.push_back(album);
    }
    return result;
}

QVector<Artist> TrackRepository::artists() const
{
    QVector<Artist> result;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT artist_key, MAX(artist), COUNT(DISTINCT album_key), COUNT(*) FROM tracks "
                                 "WHERE %1 GROUP BY artist_key "
                                 "ORDER BY CASE WHEN artist_key=? THEN 1 ELSE 0 END, MAX(artist) COLLATE NOCASE")
                      .arg(Database::libraryScope()));
    query.addBindValue(makeArtistKey({}));
    query.exec();
    while (query.next()) {
        Artist artist;
        artist.key = query.value(0).toString();
        artist.name = query.value(1).toString();
        if (artist.name.trimmed().isEmpty()) {
            artist.name = QStringLiteral("未知艺术家");
        }
        artist.albums = query.value(2).toInt();
        artist.tracks = query.value(3).toInt();
        result.push_back(artist);
    }
    return result;
}

} // namespace echo
