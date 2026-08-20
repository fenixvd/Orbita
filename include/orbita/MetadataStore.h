#pragma once

#include "orbita/Types.h"

#include <QString>
#include <QVector>
#include <optional>

struct sqlite3;

namespace orbita {

/// Дерево Диска без содержимого: отсюда FUSE отвечает на getattr и readdir,
/// не заглядывая в сеть.
///
/// SQLite напрямую, а не через QtSql: соединение QtSql принадлежит своему
/// потоку, а libfuse заводит и убивает потоки на ходу, переиспользуя их
/// идентификаторы. Здесь одно соединение обслуживает любые потоки.
class MetadataStore {
public:
    MetadataStore();
    ~MetadataStore();

    MetadataStore(const MetadataStore&) = delete;
    MetadataStore& operator=(const MetadataStore&) = delete;

    /// Открывает (при необходимости создаёт) базу и накатывает схему.
    bool open(const QString& dbPath);
    void close();
    QString lastError() const { return m_lastError; }

    /// Вставляет или обновляет узел, сохраняя текущее состояние содержимого.
    bool upsert(const Resource& res);
    /// Пакетная вставка в одной транзакции — на порядок быстрее поштучной.
    bool upsertBatch(const QVector<Resource>& items);

    std::optional<Resource> get(const QString& path) const;
    QVector<Resource> children(const QString& dirPath) const;

    bool remove(const QString& path);
    /// Удаляет узел вместе со всем поддеревом.
    bool removeRecursive(const QString& path);

    bool setState(const QString& path, ContentState state);
    bool setSize(const QString& path, qint64 size);
    /// Пустая ссылка означает, что доступ закрыт.
    bool setPublicUrl(const QString& path, const QString& url);

    /// Поиск по имени во всём дереве — по локальной базе, мгновенно.
    QVector<Resource> search(const QString& query, int limit = 500) const;
    /// Переносит узел вместе со всем поддеревом — переименование папки должно
    /// чинить пути у всего, что внутри.
    bool rename(const QString& from, const QString& to);

    /// Удаляет из папки всё, чего нет в свежем списке от сервера. Иначе файл,
    /// удалённый с другого устройства, остаётся в дереве призраком.
    int pruneMissing(const QString& dirPath, const QVector<QString>& keep);

    /// Незавершённая отправка файла с компьютера на Диск.
    struct PendingUpload {
        QString localPath;  ///< файл на компьютере — источник
        QString remotePath; ///< куда он должен попасть на Диске
        int attempts = 0;
    };

    /// Запоминает сорвавшуюся отправку, чтобы повторить её позже.
    /// Очередь живёт в той же базе, поэтому переживает перезапуск.
    bool addPendingUpload(const QString& localPath, const QString& remotePath);
    QVector<PendingUpload> pendingUploads() const;
    bool removePendingUpload(const QString& localPath);
    /// Отмечает неудачную попытку — по счётчику видно безнадёжные случаи.
    bool notePendingUploadAttempt(const QString& localPath);

    /// Сводка по содержимому папки, включая вложенные.
    struct FolderStats {
        int files = 0;     ///< всего файлов в поддереве
        int onDevice = 0;  ///< из них лежащих на устройстве
        int dirty = 0;     ///< ждущих выгрузки
    };
    /// Нужна для эмблемы папки: скачана она целиком, частично или совсем нет.
    FolderStats folderStats(const QString& dirPath) const;

    /// Все пути в заданном состоянии — кэшу нужно знать, что закреплено.
    QVector<QString> pathsWithState(ContentState state) const;

    /// Суммарный размер того, что реально лежит на диске.
    qint64 cachedBytes() const;

private:
    bool exec(const QString& sql);
    bool applySchema();

    sqlite3* m_db = nullptr;
    mutable QString m_lastError;
};

} // namespace orbita
