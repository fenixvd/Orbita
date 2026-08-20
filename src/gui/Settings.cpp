#include "Settings.h"

#include "orbita/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>

namespace orbita {
namespace {

constexpr auto kAppId = "ru.rainedev.orbita";

qint64 directorySize(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

} // namespace

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Orbita"),
                 QStringLiteral("Orbita"))
{
}

bool Settings::trayEnabled() const
{
    return m_settings.value(QStringLiteral("tray/enabled"), true).toBool();
}

void Settings::setTrayEnabled(bool enabled)
{
    if (trayEnabled() == enabled)
        return;
    m_settings.setValue(QStringLiteral("tray/enabled"), enabled);
    m_settings.sync();
    Q_EMIT trayEnabledChanged();
}

QString Settings::autostartFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/autostart/") + QLatin1String(kAppId) + QStringLiteral(".desktop");
}

bool Settings::autostart() const
{
    return QFile::exists(autostartFile());
}

void Settings::setAutostart(bool enabled)
{
    if (autostart() == enabled)
        return;

    if (!enabled) {
        QFile::remove(autostartFile());
        Q_EMIT autostartChanged();
        return;
    }

    QDir().mkpath(QFileInfo(autostartFile()).absolutePath());
    QFile file(autostartFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    // Путь к бинарнику берём текущий: клиент может быть и установлен,
    // и запущен прямо из сборочного каталога.
    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Orbita\n"
        << "Comment=Яндекс.Диск с файлами по требованию\n"
        << "Exec=" << QCoreApplication::applicationFilePath() << "\n"
        << "Icon=" << QLatin1String(kAppId) << "\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";

    Q_EMIT autostartChanged();
}

bool Settings::automount() const
{
    return m_settings.value(QStringLiteral("disk/automount"), false).toBool();
}

void Settings::setAutomount(bool enabled)
{
    if (automount() == enabled)
        return;
    m_settings.setValue(QStringLiteral("disk/automount"), enabled);
    m_settings.sync();
    Q_EMIT automountChanged();
}

QString Settings::mountPoint() const
{
    return paths::mountPoint();
}

void Settings::setMountPoint(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || trimmed == mountPoint())
        return;
    m_settings.setValue(QStringLiteral("disk/mountPoint"), trimmed);
    m_settings.sync();
    Q_EMIT mountPointChanged();
}

QString Settings::uploadFolder() const
{
    return paths::uploadFolder();
}

void Settings::setUploadFolder(const QString& path)
{
    QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return;
    // Путь на Диске всегда начинается с косой черты — поправляем молча,
    // вместо того чтобы отчитывать пользователя за формат.
    if (!trimmed.startsWith(QLatin1Char('/')))
        trimmed.prepend(QLatin1Char('/'));
    if (trimmed == uploadFolder())
        return;

    m_settings.setValue(QStringLiteral("disk/uploadFolder"), trimmed);
    m_settings.sync();
    Q_EMIT uploadFolderChanged();
}

int Settings::cacheBudgetGiB() const
{
    return static_cast<int>(paths::cacheBudget() / (1024LL * 1024 * 1024));
}

void Settings::setCacheBudgetGiB(int gib)
{
    if (gib <= 0 || gib == cacheBudgetGiB())
        return;
    m_settings.setValue(QStringLiteral("cache/budgetBytes"), gib * 1024LL * 1024 * 1024);
    m_settings.sync();
    Q_EMIT cacheBudgetChanged();
}

int Settings::pollSeconds() const
{
    return paths::pollSeconds();
}

void Settings::setPollSeconds(int seconds)
{
    if (seconds == pollSeconds())
        return;
    m_settings.setValue(QStringLiteral("sync/pollSeconds"), seconds);
    m_settings.sync();
    Q_EMIT pollSecondsChanged();
}

QString Settings::cacheSizeText() const
{
    const qint64 content = directorySize(paths::cacheDir());
    const qint64 thumbs = directorySize(paths::thumbsDir());
    return tr("%1 файлов, %2 миниатюр")
        .arg(QLocale().formattedDataSize(content), QLocale().formattedDataSize(thumbs));
}

void Settings::clearCache()
{
    QDir(paths::cacheDir()).removeRecursively();
    QDir(paths::thumbsDir()).removeRecursively();
    QDir().mkpath(paths::cacheDir());
    QDir().mkpath(paths::thumbsDir());
    Q_EMIT cacheChanged();
}

} // namespace orbita
