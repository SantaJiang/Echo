#pragma once

#include "library/types.h"

namespace echo {
class Database;

// Full-text and substring lookup. Shares the Database connection.
class SearchIndex {
public:
    explicit SearchIndex(const Database& database);
    [[nodiscard]] QVector<Track> match(const QString& ftsQuery, int limit) const;
    [[nodiscard]] QVector<Track> containing(const QString& text, int limit) const;

private:
    const Database& m_database;
};

} // namespace echo
