#pragma once

#include <QString>
#include <atomic>

struct fuse;

namespace orbita {

class MetadataStore;
class ContentProvider;
class RemoteOps;

/// Точка монтирования: дерево из метаданных, содержимое — по требованию.
/// Файловый менеджер видит весь Диск, место занимают только открытые файлы.
class FuseMount {
public:
    /// Без `ops` файловая система монтируется только для чтения.
    FuseMount(MetadataStore& meta, ContentProvider& content, RemoteOps* ops = nullptr);

    /// Блокирует вызывающий поток до размонтирования.
    ///
    /// Цикл многопоточный: файловый менеджер шлёт десятки запросов разом,
    /// и одна долгая загрузка не должна останавливать всю файловую систему.
    /// Метаданные это выдерживают — у MetadataStore своё соединение на поток.
    bool run(const QString& mountPoint, bool debug = false);

    /// Просит цикл завершиться и отцепляет точку монтирования.
    /// Можно звать из другого потока — иначе кнопку «Отключить» нажать некому.
    void stop();

    QString lastError() const { return m_lastError; }

private:
    MetadataStore& m_meta;
    ContentProvider& m_content;
    RemoteOps* m_ops = nullptr;
    std::atomic<struct fuse*> m_fuse { nullptr };
    /// Номер соединения в /sys/fs/fuse/connections — им разрываем связь,
    /// когда точку кто-то держит.
    std::atomic<int> m_connectionId { -1 };
    QString m_mountPoint;
    QString m_lastError;
};

} // namespace orbita
