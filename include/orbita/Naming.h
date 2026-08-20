#pragma once

#include <QDateTime>
#include <QString>

namespace orbita::naming {

/// Вставляет пометку перед расширением: «отчёт.txt» → «отчёт (копия).txt».
/// Точка в начале имени расширением не считается — «.bashrc» останется целым.
QString withSuffix(const QString& path, const QString& suffix);

/// Имя для копии, которую делаем вместо перезаписи чужих изменений.
/// Время в имени — чтобы несколько конфликтов подряд не затирали друг друга.
QString conflictName(const QString& path, const QDateTime& when = QDateTime::currentDateTime());

/// Имя для копии по команде «Создать копию».
QString duplicateName(const QString& path);

} // namespace orbita::naming
