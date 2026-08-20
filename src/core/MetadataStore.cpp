#include "orbita/MetadataStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <sqlite3.h>

namespace orbita {
namespace {

/// Родительский путь: "/a/b/c" -> "/a/b", "/a" -> "/".
QString parentOf(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return path.left(slash);
}

QString nameOf(const QString& path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

constexpr auto kSelectColumns = "path, name, is_dir, size, md5, modified, state, public_url";

QString columnText(sqlite3_stmt* stmt, int column)
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
    return text ? QString::fromUtf8(text) : QString();
}

Resource resourceFromRow(sqlite3_stmt* stmt)
{
    Resource r;
    r.path = columnText(stmt, 0);
    r.name = columnText(stmt, 1);
    r.isDir = sqlite3_column_int(stmt, 2) != 0;
    r.size = sqlite3_column_int64(stmt, 3);
    r.md5 = columnText(stmt, 4);
    r.modified = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(stmt, 5));
    r.state = static_cast<ContentState>(sqlite3_column_int(stmt, 6));
    r.publicUrl = columnText(stmt, 7);
    return r;
}

/// Привязка строки: SQLITE_TRANSIENT заставляет SQLite скопировать данные,
/// поэтому временный QByteArray безопасен.
void bindText(sqlite3_stmt* stmt, int index, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(stmt, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

/// Готовый запрос с автоматическим освобождением — забыть finalize нельзя,
/// иначе база останется заблокированной.
class Statement {
public:
    Statement(sqlite3* db, const QString& sql)
        : m_db(db)
    {
        const QByteArray utf8 = sql.toUtf8();
        m_ok = sqlite3_prepare_v2(db, utf8.constData(), utf8.size(), &m_stmt, nullptr) == SQLITE_OK;
    }

    ~Statement()
    {
        if (m_stmt)
            sqlite3_finalize(m_stmt);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool isValid() const { return m_ok && m_stmt; }
    sqlite3_stmt* get() const { return m_stmt; }
    QString error() const { return QString::fromUtf8(sqlite3_errmsg(m_db)); }

private:
    sqlite3* m_db = nullptr;
    sqlite3_stmt* m_stmt = nullptr;
    bool m_ok = false;
};

} // namespace

MetadataStore::MetadataStore() = default;

MetadataStore::~MetadataStore()
{
    close();
}

bool MetadataStore::open(const QString& dbPath)
{
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    // FULLMUTEX: соединением пользуются потоки FUSE, замки берёт на себя SQLite.
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &m_db, flags, nullptr) != SQLITE_OK) {
        m_lastError = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db))
                           : QStringLiteral("не удалось открыть базу");
        close();
        return false;
    }

    // Лучше подождать, чем вернуть файловой системе «база занята».
    sqlite3_busy_timeout(m_db, 5000);

    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    return applySchema();
}

void MetadataStore::close()
{
    if (!m_db)
        return;
    sqlite3_close_v2(m_db);
    m_db = nullptr;
}

bool MetadataStore::exec(const QString& sql)
{
    if (!m_db)
        return false;

    char* message = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        m_lastError = message ? QString::fromUtf8(message) : QStringLiteral("ошибка SQL");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool MetadataStore::applySchema()
{
    const bool ok = exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS nodes (
            path     TEXT PRIMARY KEY,
            parent   TEXT NOT NULL,
            name     TEXT NOT NULL,
            is_dir   INTEGER NOT NULL DEFAULT 0,
            size     INTEGER NOT NULL DEFAULT 0,
            md5      TEXT,
            modified INTEGER NOT NULL DEFAULT 0,
            state    INTEGER NOT NULL DEFAULT 0
        )
    )"));
    if (!ok)
        return false;

    // Столбец появился позже; ошибка «duplicate column» тут ожидаема.
    exec(QStringLiteral("ALTER TABLE nodes ADD COLUMN public_url TEXT"));

    // Очередь отправки: сорвавшаяся загрузка не должна теряться при выходе.
    exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS uploads (
            local_path  TEXT PRIMARY KEY,
            remote_path TEXT NOT NULL,
            attempts    INTEGER NOT NULL DEFAULT 0
        )
    )"));

    // readdir бьёт именно по parent — без индекса большие папки заметно тормозят.
    return exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_nodes_parent ON nodes(parent)"));
}

bool MetadataStore::upsert(const Resource& res)
{
    return upsertBatch({ res });
}

bool MetadataStore::upsertBatch(const QVector<Resource>& items)
{
    if (items.isEmpty())
        return true;
    if (!m_db)
        return false;

    if (!exec(QStringLiteral("BEGIN IMMEDIATE")))
        return false;

    // state не трогаем: он наш, а не серверный. Иначе обход снял бы закрепления.
    Statement stmt(m_db, QStringLiteral(R"(
        INSERT INTO nodes (path, parent, name, is_dir, size, md5, modified, state, public_url)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
        ON CONFLICT(path) DO UPDATE SET
            parent = excluded.parent,
            name = excluded.name,
            is_dir = excluded.is_dir,
            size = excluded.size,
            md5 = excluded.md5,
            modified = excluded.modified,
            public_url = excluded.public_url
    )"));
    if (!stmt.isValid()) {
        m_lastError = stmt.error();
        exec(QStringLiteral("ROLLBACK"));
        return false;
    }

    for (const Resource& r : items) {
        sqlite3_reset(stmt.get());
        bindText(stmt.get(), 1, r.path);
        bindText(stmt.get(), 2, parentOf(r.path));
        bindText(stmt.get(), 3, r.name);
        sqlite3_bind_int(stmt.get(), 4, r.isDir ? 1 : 0);
        sqlite3_bind_int64(stmt.get(), 5, r.size);
        bindText(stmt.get(), 6, r.md5);
        sqlite3_bind_int64(stmt.get(), 7, r.modified.toSecsSinceEpoch());
        sqlite3_bind_int(stmt.get(), 8, static_cast<int>(r.state));
        bindText(stmt.get(), 9, r.publicUrl);

        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            m_lastError = stmt.error();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
    }

    return exec(QStringLiteral("COMMIT"));
}

std::optional<Resource> MetadataStore::get(const QString& path) const
{
    if (!m_db)
        return std::nullopt;

    Statement stmt(m_db, QStringLiteral("SELECT %1 FROM nodes WHERE path = ?1")
                             .arg(QLatin1String(kSelectColumns)));
    if (!stmt.isValid())
        return std::nullopt;

    bindText(stmt.get(), 1, path);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        return std::nullopt;
    return resourceFromRow(stmt.get());
}

QVector<Resource> MetadataStore::children(const QString& dirPath) const
{
    QVector<Resource> out;
    if (!m_db)
        return out;

    Statement stmt(m_db,
                   QStringLiteral("SELECT %1 FROM nodes WHERE parent = ?1 "
                                  "ORDER BY is_dir DESC, name")
                       .arg(QLatin1String(kSelectColumns)));
    if (!stmt.isValid())
        return out;

    bindText(stmt.get(), 1, dirPath);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        out.append(resourceFromRow(stmt.get()));
    return out;
}

bool MetadataStore::remove(const QString& path)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral("DELETE FROM nodes WHERE path = ?1"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, path);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool MetadataStore::removeRecursive(const QString& path)
{
    if (!m_db)
        return false;

    Statement stmt(m_db,
                   QStringLiteral("DELETE FROM nodes WHERE path = ?1 OR path LIKE ?2"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, path);
    bindText(stmt.get(), 2, path + QStringLiteral("/%"));
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool MetadataStore::setState(const QString& path, ContentState state)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral("UPDATE nodes SET state = ?1 WHERE path = ?2"));
    if (!stmt.isValid())
        return false;

    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(state));
    bindText(stmt.get(), 2, path);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool MetadataStore::setSize(const QString& path, qint64 size)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral("UPDATE nodes SET size = ?1 WHERE path = ?2"));
    if (!stmt.isValid())
        return false;

    sqlite3_bind_int64(stmt.get(), 1, size);
    bindText(stmt.get(), 2, path);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool MetadataStore::setPublicUrl(const QString& path, const QString& url)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral("UPDATE nodes SET public_url = ?1 WHERE path = ?2"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, url);
    bindText(stmt.get(), 2, path);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

QVector<Resource> MetadataStore::search(const QString& query, int limit) const
{
    QVector<Resource> out;
    if (!m_db || query.isEmpty())
        return out;

    // Поиска в API Диска нет, но всё дерево и так лежит локально.
    Statement stmt(m_db,
                   QStringLiteral("SELECT %1 FROM nodes WHERE name LIKE ?1 ESCAPE '\\' "
                                  "ORDER BY is_dir DESC, name LIMIT ?2")
                       .arg(QLatin1String(kSelectColumns)));
    if (!stmt.isValid())
        return out;

    QString escaped = query;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
        .replace(QLatin1Char('%'), QLatin1String("\\%"))
        .replace(QLatin1Char('_'), QLatin1String("\\_"));

    bindText(stmt.get(), 1, QStringLiteral("%%%1%%").arg(escaped));
    sqlite3_bind_int(stmt.get(), 2, limit);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        out.append(resourceFromRow(stmt.get()));
    return out;
}

bool MetadataStore::rename(const QString& from, const QString& to)
{
    if (!m_db)
        return false;

    if (!exec(QStringLiteral("BEGIN IMMEDIATE")))
        return false;

    // Сначала потомки: у них меняется только начало пути, имя остаётся прежним.
    {
        Statement stmt(m_db, QStringLiteral(R"(
            UPDATE nodes
               SET path = ?1 || substr(path, ?2 + 1),
                   parent = ?1 || substr(parent, ?2 + 1)
             WHERE path LIKE ?3
        )"));
        if (!stmt.isValid()) {
            m_lastError = stmt.error();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        bindText(stmt.get(), 1, to);
        sqlite3_bind_int(stmt.get(), 2, from.size());
        bindText(stmt.get(), 3, from + QStringLiteral("/%"));
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            m_lastError = stmt.error();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
    }

    {
        Statement stmt(m_db, QStringLiteral(
            "UPDATE nodes SET path = ?1, parent = ?2, name = ?3 WHERE path = ?4"));
        if (!stmt.isValid()) {
            m_lastError = stmt.error();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        bindText(stmt.get(), 1, to);
        bindText(stmt.get(), 2, parentOf(to));
        bindText(stmt.get(), 3, nameOf(to));
        bindText(stmt.get(), 4, from);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            m_lastError = stmt.error();
            exec(QStringLiteral("ROLLBACK"));
            return false;
        }
    }

    return exec(QStringLiteral("COMMIT"));
}

bool MetadataStore::addPendingUpload(const QString& localPath, const QString& remotePath)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral(R"(
        INSERT INTO uploads (local_path, remote_path, attempts) VALUES (?1, ?2, 1)
        ON CONFLICT(local_path) DO UPDATE SET remote_path = excluded.remote_path
    )"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, localPath);
    bindText(stmt.get(), 2, remotePath);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

QVector<MetadataStore::PendingUpload> MetadataStore::pendingUploads() const
{
    QVector<PendingUpload> out;
    if (!m_db)
        return out;

    Statement stmt(m_db,
                   QStringLiteral("SELECT local_path, remote_path, attempts FROM uploads"));
    if (!stmt.isValid())
        return out;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        PendingUpload item;
        item.localPath = columnText(stmt.get(), 0);
        item.remotePath = columnText(stmt.get(), 1);
        item.attempts = sqlite3_column_int(stmt.get(), 2);
        out.append(item);
    }
    return out;
}

bool MetadataStore::removePendingUpload(const QString& localPath)
{
    if (!m_db)
        return false;

    Statement stmt(m_db, QStringLiteral("DELETE FROM uploads WHERE local_path = ?1"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, localPath);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool MetadataStore::notePendingUploadAttempt(const QString& localPath)
{
    if (!m_db)
        return false;

    Statement stmt(m_db,
                   QStringLiteral("UPDATE uploads SET attempts = attempts + 1 "
                                  "WHERE local_path = ?1"));
    if (!stmt.isValid())
        return false;

    bindText(stmt.get(), 1, localPath);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

MetadataStore::FolderStats MetadataStore::folderStats(const QString& dirPath) const
{
    FolderStats stats;
    if (!m_db)
        return stats;

    // Диапазон вместо LIKE — путь это первичный ключ, сравнение идёт по индексу.
    // '0' — следующий символ после '/', отсюда верхняя граница.
    const QString prefix = (dirPath == QLatin1String("/")) ? dirPath
                                                           : dirPath + QLatin1Char('/');
    QString upper = prefix;
    upper.chop(1);
    upper += QLatin1Char('0');

    Statement stmt(m_db, QStringLiteral(R"(
        SELECT COUNT(*),
               COALESCE(SUM(state <> 0), 0),
               COALESCE(SUM(state = 3), 0)
          FROM nodes
         WHERE is_dir = 0 AND path >= ?1 AND path < ?2
    )"));
    if (!stmt.isValid())
        return stats;

    bindText(stmt.get(), 1, prefix);
    bindText(stmt.get(), 2, upper);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        return stats;

    stats.files = sqlite3_column_int(stmt.get(), 0);
    stats.onDevice = sqlite3_column_int(stmt.get(), 1);
    stats.dirty = sqlite3_column_int(stmt.get(), 2);
    return stats;
}

int MetadataStore::pruneMissing(const QString& dirPath, const QVector<QString>& keep)
{
    if (!m_db)
        return 0;

    // Разницу считаем в C++: у SQLite предел на число подставляемых значений.
    const QSet<QString> alive(keep.cbegin(), keep.cend());

    QVector<Resource> stale;
    for (const Resource& child : children(dirPath)) {
        if (!alive.contains(child.path))
            stale.append(child);
    }
    if (stale.isEmpty())
        return 0;

    exec(QStringLiteral("BEGIN IMMEDIATE"));
    for (const Resource& gone : stale) {
        // У папки уносим и всё её содержимое.
        if (gone.isDir)
            removeRecursive(gone.path);
        else
            remove(gone.path);
    }
    exec(QStringLiteral("COMMIT"));
    return stale.size();
}

QVector<QString> MetadataStore::pathsWithState(ContentState state) const
{
    QVector<QString> out;
    if (!m_db)
        return out;

    Statement stmt(m_db, QStringLiteral("SELECT path FROM nodes WHERE state = ?1"));
    if (!stmt.isValid())
        return out;

    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(state));
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        out.append(columnText(stmt.get(), 0));
    return out;
}

qint64 MetadataStore::cachedBytes() const
{
    if (!m_db)
        return 0;

    Statement stmt(m_db, QStringLiteral(
        "SELECT COALESCE(SUM(size), 0) FROM nodes WHERE state IN (?1, ?2)"));
    if (!stmt.isValid())
        return 0;

    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(ContentState::Cached));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(ContentState::Pinned));
    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        return 0;
    return sqlite3_column_int64(stmt.get(), 0);
}

} // namespace orbita
