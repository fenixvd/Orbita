#include "orbita/Paths.h"

#include <QSettings>
#include <QStandardPaths>

namespace orbita::paths {
namespace {

/// Настройки читаются часто (плагин Dolphin спрашивает точку подключения
/// на каждый файл), поэтому держим один объект на процесс.
QSettings& settings()
{
    static QSettings instance(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("Orbita"), QStringLiteral("Orbita"));
    return instance;
}

QString homePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

} // namespace

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Orbita");
}

QString databaseFile()
{
    return dataDir() + QStringLiteral("/metadata.db");
}

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/Orbita/content");
}

QString thumbsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/Orbita/thumbs");
}

QString settingsFile()
{
    return settings().fileName();
}

QString mountPoint()
{
    // sync() перед чтением: настройки мог изменить другой процесс —
    // например, окно, пока работает плагин Dolphin.
    settings().sync();
    const QString configured = settings().value(QStringLiteral("disk/mountPoint")).toString();
    return configured.isEmpty() ? homePath() + QStringLiteral("/Яндекс.Диск") : configured;
}

QString uploadFolder()
{
    settings().sync();
    const QString configured = settings().value(QStringLiteral("disk/uploadFolder")).toString();
    return configured.isEmpty() ? QStringLiteral("/Загрузки") : configured;
}

qint64 cacheBudget()
{
    settings().sync();
    constexpr qint64 kDefault = 5LL * 1024 * 1024 * 1024;
    const qint64 value = settings().value(QStringLiteral("cache/budgetBytes"), kDefault).toLongLong();
    return value > 0 ? value : kDefault;
}

int pollSeconds()
{
    settings().sync();
    const int value = settings().value(QStringLiteral("sync/pollSeconds"), 60).toInt();
    // Слишком частый опрос — это лишняя нагрузка на API без всякой пользы.
    return qBound(15, value, 3600);
}

} // namespace orbita::paths
