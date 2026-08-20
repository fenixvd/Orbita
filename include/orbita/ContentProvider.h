#pragma once

#include <QtGlobal>

#include <QString>
#include <functional>

namespace orbita {

/// Источник содержимого файлов.
///
/// Абстракция нужна, чтобы FUSE-слой не знал ничего про сеть: в бою это
/// загрузчик с Яндекс.Диска, в тестах — подделка на локальных файлах.
class ContentProvider {
public:
    virtual ~ContentProvider() = default;

    /// Прогресс скачивания: (получено, всего). Всего может быть -1, если неизвестно.
    using ProgressFn = std::function<void(qint64, qint64)>;

    /// Гарантирует, что содержимое `path` лежит локально, и возвращает путь к нему.
    /// Пустая строка — ошибка. Вызов блокирующий: FUSE-поток и должен ждать.
    virtual QString materialize(const QString& path, const ProgressFn& onProgress = {}) = 0;

    /// Уже есть локально? Позволяет открыть файл, не трогая сеть.
    virtual bool isMaterialized(const QString& path) const = 0;

    /// Читает кусок файла. Если содержимое локально — из него, иначе из сети
    /// запросом диапазона, без скачивания файла целиком.
    /// Возвращает число прочитанных байт или -1 при ошибке.
    virtual qint64 readRange(const QString& path, qint64 offset, qint64 size, char* buffer) = 0;

    /// Путь к локальной копии, если источник её вообще заводит.
    virtual QString localFile(const QString& path) const
    {
        Q_UNUSED(path);
        return {};
    }

    /// Подготовка файла к записи. По умолчанию источник только для чтения.
    virtual QString prepareForWrite(const QString& path, bool truncate)
    {
        Q_UNUSED(path);
        Q_UNUSED(truncate);
        return {};
    }

    /// Перенос локальной копии вслед за переименованием.
    virtual bool renameLocal(const QString& from, const QString& to)
    {
        Q_UNUSED(from);
        Q_UNUSED(to);
        return false;
    }
};

} // namespace orbita
