#include "orbita/CacheManager.h"

#include "orbita/MetadataStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>

namespace orbita {
namespace {

/// Имя в кэше — хеш пути: воспроизводить дерево Диска локально незачем.
QString cacheKey(const QString& remotePath)
{
    const QByteArray hash = QCryptographicHash::hash(remotePath.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex().left(40));
}

/// Считает md5 файла потоком — файлы бывают в гигабайты.
bool checksumMatches(const QString& file, const QString& expected)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&f))
        return false;

    return QString::fromLatin1(hash.result().toHex()) == expected.toLower();
}

} // namespace

CacheManager::CacheManager(QString cacheDir, MetadataStore* meta)
    : m_cacheDir(std::move(cacheDir))
    , m_meta(meta)
{
    QDir().mkpath(m_cacheDir);
}

QString CacheManager::localPathFor(const QString& remotePath) const
{
    return m_cacheDir + QLatin1Char('/') + cacheKey(remotePath);
}

bool CacheManager::isMaterialized(const QString& path) const
{
    return QFileInfo::exists(localPathFor(path));
}

std::shared_ptr<QMutex> CacheManager::lockFor(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pathLocks.find(path);
    if (it == m_pathLocks.end())
        it = m_pathLocks.insert(path, std::make_shared<QMutex>());
    return *it;
}

QString CacheManager::materialize(const QString& path, const ProgressFn& onProgress)
{
    const QString local = localPathFor(path);
    if (QFileInfo::exists(local))
        return local;

    if (!m_fetcher)
        return {};

    // Замок на файл, а не на весь кэш — иначе одна загрузка держит все.
    const std::shared_ptr<QMutex> fileLock = lockFor(path);
    QMutexLocker lock(fileLock.get());

    // Пока ждали замок, файл мог скачать другой поток.
    if (QFileInfo::exists(local))
        return local;

    // Через .part: оборванная загрузка не должна сойти за готовый файл.
    const QString partial = local + QStringLiteral(".part");
    QFile::remove(partial);

    if (!m_fetcher(path, partial, onProgress)) {
        QFile::remove(partial);
        return {};
    }

    // Сверяем с контрольной суммой из метаданных: оборванная закачка,
    // которую HTTP счёл успешной, иначе легла бы в кэш как настоящий файл.
    if (m_meta) {
        const auto res = m_meta->get(path);
        if (res && !res->md5.isEmpty() && !checksumMatches(partial, res->md5)) {
            QFile::remove(partial);
            return {};
        }
    }

    if (!QFile::rename(partial, local)) {
        QFile::remove(partial);
        return {};
    }

    if (m_meta) {
        const auto res = m_meta->get(path);
        if (res && res->state != ContentState::Pinned)
            m_meta->setState(path, ContentState::Cached);
    }

    lock.unlock();
    enforceBudget();
    return local;
}

qint64 CacheManager::readRange(const QString& path, qint64 offset, qint64 size, char* buffer)
{
    // Файл уже на устройстве — сеть не нужна.
    const QString local = localPathFor(path);
    QFile file(local);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        if (!file.seek(offset))
            return 0;
        return file.read(buffer, size);
    }

    if (m_rangeFetcher)
        return m_rangeFetcher(path, offset, size, buffer);

    // Источник не умеет диапазоны — придётся забрать файл целиком.
    const QString materialized = materialize(path);
    if (materialized.isEmpty())
        return -1;

    QFile whole(materialized);
    if (!whole.open(QIODevice::ReadOnly) || !whole.seek(offset))
        return -1;
    return whole.read(buffer, size);
}

QString CacheManager::prepareForWrite(const QString& path, bool truncate)
{
    if (!truncate && !isMaterialized(path)) {
        // Дописать кусок API не умеет, поэтому нужен файл целиком.
        const QString local = materialize(path);
        if (!local.isEmpty())
            return local;
    }

    QMutexLocker lock(&m_mutex);
    const QString local = localPathFor(path);
    QFile f(local);
    if (!f.open(QIODevice::ReadWrite | (truncate ? QIODevice::Truncate : QIODevice::NotOpen)))
        return {};
    f.close();

    if (m_meta)
        m_meta->setState(path, ContentState::Dirty);
    return local;
}

bool CacheManager::renameLocal(const QString& from, const QString& to)
{
    QMutexLocker lock(&m_mutex);
    const QString src = localPathFor(from);
    if (!QFileInfo::exists(src))
        return true; // локальной копии не было — переносить нечего

    const QString dst = localPathFor(to);
    QFile::remove(dst);
    return QFile::rename(src, dst);
}

bool CacheManager::pin(const QString& path)
{
    if (materialize(path).isEmpty())
        return false;
    return m_meta ? m_meta->setState(path, ContentState::Pinned) : true;
}

bool CacheManager::unpin(const QString& path)
{
    return m_meta ? m_meta->setState(path, ContentState::Cached) : true;
}

bool CacheManager::evict(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    const QString local = localPathFor(path);
    if (QFileInfo::exists(local) && !QFile::remove(local))
        return false;
    return m_meta ? m_meta->setState(path, ContentState::Placeholder) : true;
}

qint64 CacheManager::enforceBudget()
{
    if (m_budget <= 0)
        return 0;

    QMutexLocker lock(&m_mutex);

    QDir dir(m_cacheDir);
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::NoSort);

    qint64 total = 0;
    for (const QFileInfo& fi : files)
        total += fi.size();
    if (total <= m_budget)
        return 0;

    // LRU по времени последнего доступа: что дольше всех не открывали, то и уходит.
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.fileTime(QFileDevice::FileAccessTime) < b.fileTime(QFileDevice::FileAccessTime);
    });

    // Закреплённое не вытесняем. Имена в кэше — хеши, поэтому переводим пути.
    QSet<QString> protectedKeys;
    if (m_meta) {
        for (const QString& pinned : m_meta->pathsWithState(ContentState::Pinned))
            protectedKeys.insert(cacheKey(pinned));
    }

    qint64 freed = 0;
    for (const QFileInfo& fi : files) {
        if (total - freed <= m_budget)
            break;
        if (fi.fileName().endsWith(QStringLiteral(".part")))
            continue;
        if (protectedKeys.contains(fi.fileName()))
            continue;
        const qint64 size = fi.size();
        if (QFile::remove(fi.absoluteFilePath()))
            freed += size;
    }
    return freed;
}

} // namespace orbita
