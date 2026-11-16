#include "library/schema.h"
#include "library/database.h"

#include <QMutex>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace echo {
namespace {
const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS roots (
  id INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE,
  name TEXT, active INTEGER NOT NULL DEFAULT 1, revision INTEGER NOT NULL DEFAULT 1,
  status TEXT NOT NULL DEFAULT 'available', message TEXT, mount_path TEXT, checked_at INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS tracks (
  id INTEGER PRIMARY KEY,
  root_id INTEGER,
  path TEXT NOT NULL,
  folder TEXT,
  title TEXT,
  artist TEXT,
  album_artist TEXT,
  album TEXT,
  album_key TEXT,
  artist_key TEXT,
  year INTEGER,
  track_number INTEGER,
  disc_number INTEGER,
  duration_ms INTEGER,
  sample_rate INTEGER,
  bit_depth INTEGER,
  channels INTEGER,
  codec TEXT,
  has_track_gain INTEGER,
  has_album_gain INTEGER,
  track_gain_db REAL,
  album_gain_db REAL,
  track_peak REAL,
  album_peak REAL,
  lyrics TEXT,
  artwork_hash TEXT,
  mtime INTEGER,
  file_size INTEGER,
  missing INTEGER NOT NULL DEFAULT 0,
  cue_path TEXT,
  cue_number INTEGER,
  cue_start_ms INTEGER NOT NULL DEFAULT -1,
  cue_end_ms INTEGER,
  added_at INTEGER NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_tracks_identity ON tracks(path, cue_start_ms);
CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album_key);
CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist_key);
CREATE INDEX IF NOT EXISTS idx_tracks_folder ON tracks(folder);
CREATE INDEX IF NOT EXISTS idx_tracks_root ON tracks(root_id);
CREATE TABLE IF NOT EXISTS playlist_folders (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  parent_id INTEGER,
  sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS playlists (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  folder_id INTEGER,
  sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS playlist_items (
  id INTEGER PRIMARY KEY,
  playlist_id INTEGER NOT NULL,
  track_id INTEGER NOT NULL,
  sort_order INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT
);
)SQL";
}

bool SchemaMigrator::migrate(QSqlDatabase& database, bool readOnly, bool* fullText, QString* error)
{
    QSqlQuery query(database);
    if (readOnly) {
        query.exec(QStringLiteral("PRAGMA busy_timeout=1000"));
        const bool ready = query.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name='tracks_fts'")) && query.next();
        if (fullText)
            *fullText = ready;
        return true;
    }
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    for (const QString& statement : QString::fromUtf8(kSchema).split(u';', Qt::SkipEmptyParts)) {
        if (statement.trimmed().isEmpty())
            continue;
        if (!query.exec(statement)) {
            if (error)
                *error = query.lastError().text();
            return false;
        }
    }
    for (const auto& column : QStringList { QStringLiteral("name TEXT"), QStringLiteral("active INTEGER NOT NULL DEFAULT 1"),
             QStringLiteral("revision INTEGER NOT NULL DEFAULT 1"), QStringLiteral("status TEXT NOT NULL DEFAULT 'available'"),
             QStringLiteral("message TEXT"), QStringLiteral("mount_path TEXT"),
             QStringLiteral("checked_at INTEGER NOT NULL DEFAULT 0") }) {
        const auto name = column.section(u' ', 0, 0);
        QSqlQuery info(database);
        info.exec(QStringLiteral("PRAGMA table_info(roots)"));
        bool found = false;
        while (info.next())
            found |= info.value(1).toString() == name;
        if (!found && !query.exec(QStringLiteral("ALTER TABLE roots ADD COLUMN ") + column)) {
            if (error)
                *error = query.lastError().text();
            return false;
        }
    }
    QMutexLocker locker(&Database::writeLock());
    if (!database.transaction())
        return false;
    auto fail = [&] {
        if (error)
            *error = query.lastError().text();
        database.rollback();
        return false;
    };
    bool hasSystemKey = false;
    query.exec(QStringLiteral("PRAGMA table_info(playlists)"));
    while (query.next())
        hasSystemKey |= query.value(1).toString() == QLatin1String("system_key");
    if (!hasSystemKey && !query.exec(QStringLiteral("ALTER TABLE playlists ADD COLUMN system_key TEXT")))
        return fail();
    const QStringList statements {
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_playlist_system_key ON playlists(system_key)"),
        QStringLiteral("INSERT OR IGNORE INTO playlists(name, sort_order, system_key) VALUES ('我喜欢的', 0, 'favorites')"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS tracks_cleanup_playlists AFTER DELETE ON tracks BEGIN DELETE FROM playlist_items WHERE track_id=old.id; END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS protect_system_playlist_delete BEFORE DELETE ON playlists WHEN old.system_key IS NOT NULL BEGIN SELECT RAISE(ABORT, 'System playlist is protected'); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS protect_system_playlist_update BEFORE UPDATE ON playlists WHEN old.system_key IS NOT NULL BEGIN SELECT RAISE(ABORT, 'System playlist is protected'); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS unique_favorite BEFORE INSERT ON playlist_items WHEN new.playlist_id=(SELECT id FROM playlists WHERE system_key='favorites') AND EXISTS(SELECT 1 FROM playlist_items WHERE playlist_id=new.playlist_id AND track_id=new.track_id) BEGIN SELECT RAISE(ABORT, 'Duplicate favorite'); END")
    };
    for (const auto& statement : statements)
        if (!query.exec(statement))
            return fail();
    if (!database.commit())
        return fail();
    const bool fts = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5("
        "title, artist, album_artist, album, folder, content='tracks', content_rowid='id', "
        "tokenize='unicode61 remove_diacritics 2')"));
    if (fts) {
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN "
            "INSERT INTO tracks_fts(rowid, title, artist, album_artist, album, folder) "
            "VALUES (new.id, new.title, new.artist, new.album_artist, new.album, new.folder); END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN "
            "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album_artist, album, folder) "
            "VALUES ('delete', old.id, old.title, old.artist, old.album_artist, old.album, old.folder); END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN "
            "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album_artist, album, folder) "
            "VALUES ('delete', old.id, old.title, old.artist, old.album_artist, old.album, old.folder); "
            "INSERT INTO tracks_fts(rowid, title, artist, album_artist, album, folder) "
            "VALUES (new.id, new.title, new.artist, new.album_artist, new.album, new.folder); END"));
    }
    if (fullText)
        *fullText = fts;
    return true;
}

} // namespace echo
