#pragma once

#include <QSqlDatabase>
#include <QString>

namespace echo {

class SchemaMigrator {
public:
    static bool migrate(QSqlDatabase& database, bool readOnly, bool* fullText, QString* error);
};

} // namespace echo
