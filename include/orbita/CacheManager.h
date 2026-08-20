#pragma once

#include "orbita/ContentProvider.h"
#include "orbita/Types.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <memory>

namespace orbita {

class MetadataStore;

/// Кэш содержимого: загрузка по требованию и вытеснение по LRU.
/// Сеть не зашита — качает переданная функция, поэтому слой FUSE можно
/// прогонять без единого запроса к Яндексу.
class CacheManager : public ContentProvider {
public:
    /// Скачать `remotePath` в файл `destFile`. Возвращает успех.
    using FetchFn = std::function<bool(const QString& remotePath,
                                       const QString& destFile,
                                       const ProgressFn& onProgress)>;

    CacheManager(QString cacheDir, MetadataStore* meta);

    void setFetcher(FetchFn fetcher) { m_fetcher = std::move(fetcher); }
    /// Потолок кэша в байтах; 0 — без ограничения. Закреплённое не вытесняется.
    void setBudget(qint64 bytes) { m_budget = bytes; }

    /// Чтение куска прямо из сети, когда файла нет локально.
    using RangeFn = std::function<qint64(const QString& remotePath,
                                         qint64 offset,
                                         qint64 size,
                                         char* buffer)>;
    void setRangeFetcher(RangeFn fetcher) { m_rangeFetcher = std::move(fetcher); }

    QString materialize(const QString& path, const ProgressFn& onProgress = {}) override;
    bool isMaterialized(const QString& path) const override;
    qint64 readRange(const QString& path, qint64 offset, qint64 size, char* buffer) override;
    QString localFile(const QString& path) const override { return localPathFor(path); }

    /// Путь к локальной копии — существует она или нет.
    QString localPathFor(const QString& remotePath) const;

    /// Готовит локальный файл под запись: существующий скачивает, новый создаёт пустым.
    /// truncate=true пропускает загрузку — содержимое всё равно будет затёрто.
    QString prepareForWrite(const QString& path, bool truncate) override;

    /// Переносит локальную копию за переименованным файлом, чтобы кэш не потерялся.
    bool renameLocal(const QString& from, const QString& to) override;

    /// Закрепить офлайн: скачать сейчас и никогда не вытеснять.
    bool pin(const QString& path);
    /// Снять закрепление; файл остаётся в кэше как обычный.
    bool unpin(const QString& path);
    /// Освободить место: удалить локальную копию, оставив заглушку.
    bool evict(const QString& path);

    /// Вытесняет самые давно не открывавшиеся файлы, пока не влезем в бюджет.
    qint64 enforceBudget();

private:
    /// Замок на конкретный файл. Два открытия одного файла ждут одну загрузку,
    /// но загрузка чужого файла никого не задерживает.
    std::shared_ptr<QMutex> lockFor(const QString& path);

    QString m_cacheDir;
    MetadataStore* m_meta = nullptr;
    FetchFn m_fetcher;
    RangeFn m_rangeFetcher;
    qint64 m_budget = 0;
    /// Защищает только сам кэш замков и учёт места — не сами загрузки.
    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<QMutex>> m_pathLocks;
};

} // namespace orbita
