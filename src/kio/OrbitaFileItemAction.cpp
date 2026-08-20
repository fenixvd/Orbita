#include "orbita/MetadataStore.h"
#include "orbita/Paths.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItemListProperties>
#include <KPluginFactory>

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>

/// Пункты Orbita в контекстном меню файлового менеджера.
///
/// Не .desktop-меню, а плагин: только так набор действий может зависеть от
/// того, куда пользователь нажал. Файлу вне Диска предлагать «сохранить на
/// устройстве» бессмысленно — ему нужна загрузка НА Диск, и наоборот.
class OrbitaFileItemAction : public KAbstractFileItemActionPlugin {
    Q_OBJECT

public:
    using KAbstractFileItemActionPlugin::KAbstractFileItemActionPlugin;

    QList<QAction*> actions(const KFileItemListProperties& properties,
                            QWidget* parentWidget) override
    {
        const QList<QUrl> urls = properties.urlList();
        if (urls.isEmpty() || !properties.isLocal())
            return {};

        const QString mount = orbita::paths::mountPoint();
        int insideDisk = 0;
        QStringList paths;
        for (const QUrl& url : urls) {
            const QString path = url.toLocalFile();
            paths.append(path);
            if (path.startsWith(mount))
                ++insideDisk;
        }

        // Смешанный выбор пропускаем: показывать половину действий, которые
        // сработают лишь для части файлов, хуже, чем не показывать ничего.
        const bool allInside = insideDisk == urls.size();
        const bool allOutside = insideDisk == 0;
        if (!allInside && !allOutside)
            return {};

        auto* menu = new QMenu(parentWidget);
        if (allInside)
            fillDiskActions(menu, paths);
        else
            fillUploadAction(menu, paths);

        auto* root = new QAction(QIcon::fromTheme(QStringLiteral("ru.rainedev.orbita")),
                                 QStringLiteral("Orbita"), parentWidget);
        root->setMenu(menu);
        return { root };
    }

private:
    /// Запускает нашу консольную команду. Плагин живёт внутри Dolphin,
    /// поэтому долгую работу делать здесь нельзя — файловый менеджер замрёт.
    static void run(const QString& command, const QStringList& paths)
    {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("orbita"));
        QProcess::startDetached(program.isEmpty() ? QStringLiteral("orbita") : program,
                                QStringList { command } + paths);
    }

    void fillDiskActions(QMenu* menu, const QStringList& paths)
    {
        addAction(menu, QStringLiteral("Сохранить на устройстве"),
                  QStringLiteral("download"), QStringLiteral("pin"), paths);
        addAction(menu, QStringLiteral("Убрать с устройства"),
                  QStringLiteral("edit-delete-remove"), QStringLiteral("unpin"), paths);
        menu->addSeparator();
        addAction(menu, QStringLiteral("Поделиться ссылкой"),
                  QStringLiteral("emblem-shared"), QStringLiteral("share"), paths);
        addAction(menu, QStringLiteral("Закрыть доступ по ссылке"),
                  QStringLiteral("object-locked"), QStringLiteral("unshare"), paths);
    }

    void fillUploadAction(QMenu* menu, const QStringList& paths)
    {
        addAction(menu, QStringLiteral("Загрузить на Яндекс.Диск"),
                  QStringLiteral("cloud-upload"), QStringLiteral("upload"), paths);
    }

    void addAction(QMenu* menu, const QString& title, const QString& icon,
                   const QString& command, const QStringList& paths)
    {
        QAction* action = menu->addAction(QIcon::fromTheme(icon), title);
        QObject::connect(action, &QAction::triggered, action,
                         [command, paths] { run(command, paths); });
    }
};

K_PLUGIN_CLASS_WITH_JSON(OrbitaFileItemAction, "orbita-fileitemaction.json")

#include "OrbitaFileItemAction.moc"
