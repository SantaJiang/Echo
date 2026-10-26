#include "library/searchindex.h"

#include "library/database.h"

#include <QSqlQuery>

namespace echo {

SearchIndex::SearchIndex(const Database& database)
    : m_database(database)
{
}

QVector<Track> SearchIndex::match(const QString& ftsQuery, int limit) const
{
    if (!m_database.hasFts() || ftsQuery.isEmpty())
        return {};
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("SELECT t.id FROM tracks_fts f JOIN tracks t ON t.id=f.rowid JOIN roots r ON "
                                 "r.id=t.root_id AND r.active=1 WHERE tracks_fts MATCH ? LIMIT ?"));
    query.addBindValue(ftsQuery);
    query.addBindValue(limit);
    if (!query.exec())
        return {};
    QVector<qint64> ids;
    while (query.next())
        ids.push_back(query.value(0).toLongLong());
    return m_database.tracksByIds(ids);
}

QVector<Track> SearchIndex::containing(const QString& text, int limit) const
{
    QSqlQuery query(m_database.connection());
    if (limit <= 0
        || !query.exec(QStringLiteral("SELECT id,title,artist,album FROM tracks WHERE ") + Database::libraryScope()
            + QStringLiteral(" ORDER BY title,id")))
        return {};
    QVector<qint64> ids;
    while (query.next()) {
        if (query.value(1).toString().contains(text, Qt::CaseInsensitive)
            || query.value(2).toString().contains(text, Qt::CaseInsensitive)
            || query.value(3).toString().contains(text, Qt::CaseInsensitive)) {
            ids << query.value(0).toLongLong();
            if (ids.size() >= limit)
                break;
        }
    }
    return m_database.tracksByIds(ids);
}

} // namespace echo
