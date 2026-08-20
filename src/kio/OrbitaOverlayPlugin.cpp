#include "orbita/MetadataStore.h"
#include "orbita/Paths.h"

#include <KOverlayIconPlugin>

#include <QUrl>

/// Эмблемы состояния файлов Диска прямо в файловом менеджере.
///
/// Dolphin спрашивает плагин про каждый показываемый файл, поэтому здесь
/// нельзя ходить в сеть — только смотреть в локальную базу метаданных.
/// Ровно так же устроены значки синхронизации у Nextcloud.
class OrbitaOverlayPlugin : public KOverlayIconPlugin {
    Q_PLUGIN_METADATA(IID "org.kde.overlayicon.orbita")
    Q_OBJECT

public:
    OrbitaOverlayPlugin()
    {
        m_ready = m_store.open(orbita::paths::databaseFile());
        m_mountPoint = orbita::paths::mountPoint();
    }

    QStringList getOverlays(const QUrl& url) override
    {
        if (!m_ready || !url.isLocalFile())
            return {};

        const QString path = url.toLocalFile();
        // Всё, что вне нашей точки подключения, нас не касается.
        if (!path.startsWith(m_mountPoint))
            return {};

        QString diskPath = path.mid(m_mountPoint.size());
        if (diskPath.isEmpty())
            return {};

        const auto resource = m_store.get(diskPath);
        if (!resource)
            return {};

        if (resource->isDir)
            return folderOverlay(diskPath);

        switch (resource->state) {
        case orbita::ContentState::Cached:
            return { QStringLiteral("vcs-normal") };
        case orbita::ContentState::Pinned:
            return { QStringLiteral("pin") };
        case orbita::ContentState::Dirty:
            return { QStringLiteral("vcs-locally-modified") };
        case orbita::ContentState::Placeholder:
            break;
        }
        // Файл существует, но лежит только в облаке — самое частое состояние,
        // и именно оно объясняет, почему папка не занимает места.
        return { QStringLiteral("cloud-download") };
    }

private:
    /// Состояние папки складывается из её содержимого: скачана целиком,
    /// частично или не скачана вовсе. Считается одним запросом по индексу.
    QStringList folderOverlay(const QString& diskPath)
    {
        const auto stats = m_store.folderStats(diskPath);
        if (stats.files == 0)
            return {}; // пустой папке сказать нечего

        if (stats.dirty > 0)
            return { QStringLiteral("vcs-locally-modified") };
        if (stats.onDevice == 0)
            return { QStringLiteral("cloud-download") };
        if (stats.onDevice == stats.files)
            return { QStringLiteral("vcs-normal") };
        // Часть содержимого уже на устройстве — самое частое состояние
        // у папок, и его важно отличать от обоих крайних.
        return { QStringLiteral("vcs-update-required") };
    }

    orbita::MetadataStore m_store;
    QString m_mountPoint;
    bool m_ready = false;
};

#include "OrbitaOverlayPlugin.moc"
