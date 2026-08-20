#pragma once

#include <QDateTime>
#include <QString>

namespace orbita {

/// Состояние содержимого файла на этой машине.
/// По умолчанию Placeholder: файл виден, но занимает ноль байт.
enum class ContentState {
    Placeholder, ///< известны только метаданные, содержимого нет
    Cached,      ///< содержимое скачано, может быть вытеснено по LRU
    Pinned,      ///< закреплён пользователем: качаем сразу, не вытесняем
    Dirty,       ///< изменён локально, ждёт выгрузки на Диск
};

/// Узел дерева Диска — файл или папка.
struct Resource {
    QString path;      ///< путь от корня Диска, всегда с ведущим "/"
    QString name;
    bool isDir = false;
    qint64 size = 0;
    QString md5;       ///< для файлов; служит признаком изменения на сервере
    QDateTime modified;
    ContentState state = ContentState::Placeholder;
    /// Непустая — ресурс опубликован и доступен по этой ссылке.
    QString publicUrl;
    /// Откуда файл попал в Корзину. Заполняется только для её содержимого.
    QString originPath;

    bool isValid() const { return !path.isEmpty(); }
};

} // namespace orbita
