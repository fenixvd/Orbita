#include "orbita/Naming.h"

namespace orbita::naming {

QString withSuffix(const QString& path, const QString& suffix)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const int dot = path.lastIndexOf(QLatin1Char('.'));

    // dot > slash + 1 отсекает два случая: точки в именах папок выше по пути
    // и файлы вида «.bashrc», где точка начинает имя, а не расширение.
    if (dot > slash + 1)
        return path.left(dot) + suffix + path.mid(dot);
    return path + suffix;
}

QString conflictName(const QString& path, const QDateTime& when)
{
    return withSuffix(path, QStringLiteral(" (конфликт %1)")
                                .arg(when.toString(QStringLiteral("yyyy-MM-dd HH-mm"))));
}

QString duplicateName(const QString& path)
{
    return withSuffix(path, QStringLiteral(" (копия)"));
}

} // namespace orbita::naming
