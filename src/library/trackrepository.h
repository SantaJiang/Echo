#pragma once

#include "library/database.h"

namespace echo {

class TrackRepository {
public:
    explicit TrackRepository(const Database& database) : m_database(database) {}
    [[nodiscard]] Track trackFromQuery(const QSqlQuery& query) const;
    [[nodiscard]] Track track(qint64 id) const;
    [[nodiscard]] QVector<Track> tracksByIds(const QVector<qint64>& ids) const;
    [[nodiscard]] QVector<Track> queryTracks(const QString& whereSql, const QVariantList& binds, const QString& orderSql) const;
    [[nodiscard]] QVector<Album> albums(const QString& artistKey) const;
    [[nodiscard]] QVector<Artist> artists() const;

private:
    [[nodiscard]] QSqlDatabase database() const { return m_database.connection(); }
    const Database& m_database;
};

} // namespace echo
