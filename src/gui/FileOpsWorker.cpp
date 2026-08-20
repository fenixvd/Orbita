#include "FileOpsWorker.h"

#include "orbita/CacheManager.h"
#include "orbita/DiskApi.h"
#include "orbita/MetadataStore.h"
#include "orbita/Naming.h"
#include "orbita/Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUrl>

namespace orbita {
namespace {

/// Склеивает путь на Диске без задвоенных косых черт.
QString joinPath(const QString& dir, const QString& name)
{
    if (dir.endsWith(QLatin1Char('/')))
        return dir + name;
    return dir + QLatin1Char('/') + name;
}

} // namespace

int FileOpsWorker::countFiles(const QString& localPath) const
{
    const QFileInfo info(localPath);
    if (!info.isDir())
        return 1;

    int count = 0;
    QDirIterator it(localPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

bool FileOpsWorker::uploadEntry(const QString& localPath, const QString& destDir,
                                int* uploaded, int total)
{
    const QFileInfo info(localPath);
    const QString target = joinPath(destDir, info.fileName());

    if (!info.isDir()) {
        // Файлы пачки плюс доля текущего — иначе на большом файле полоса стоит.
        const qreal base = total > 0 ? static_cast<qreal>(*uploaded) / total : -1;
        const auto onProgress = [this, &info, base, total](qint64 sent, qint64 size) {
            const qreal share = size > 0 && total > 0
                ? base + (static_cast<qreal>(sent) / size) / total
                : base;
            Q_EMIT progress(tr("Отправка %1").arg(info.fileName()), share);
        };

        Q_EMIT progress(tr("Отправка %1").arg(info.fileName()), base);
        if (!DiskApi::uploadBlocking(m_token, target, localPath, onProgress)) {
            // В очередь: попробуем позже, терять файл нельзя.
            MetadataStore store;
            if (store.open(paths::databaseFile()))
                store.addPendingUpload(info.absoluteFilePath(), target);
            return false;
        }
        ++(*uploaded);
        return true;
    }

    // Папку заводим на Диске и повторяем для всего, что внутри.
    // Ошибку создания игнорируем: папка могла уже существовать.
    DiskApi::mkdirBlocking(m_token, target);

    const QDir dir(localPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs
                                                    | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& child : entries) {
        if (!uploadEntry(child.absoluteFilePath(), target, uploaded, total))
            return false;
    }
    return true;
}

void FileOpsWorker::upload(const QStringList& localPaths, const QString& destDir)
{
    int total = 0;
    for (const QString& path : localPaths)
        total += countFiles(path);

    // Одна неудача не отменяет остальные файлы пачки.
    int uploaded = 0;
    int failed = 0;
    for (const QString& path : localPaths) {
        if (!uploadEntry(path, destDir, &uploaded, total))
            ++failed;
    }

    if (failed == 0)
        Q_EMIT done(true, tr("Отправлено файлов: %1").arg(uploaded));
    else
        Q_EMIT done(false, tr("Отправлено %1, отложено до следующей попытки: %2")
                               .arg(uploaded).arg(failed));
    Q_EMIT treeChanged();
}

void FileOpsWorker::uploadFromUrl(const QString& sourceUrl, const QString& destDir,
                                  const QString& name)
{
    // Нет своего имени — берём из ссылки.
    QString fileName = name.trimmed();
    if (fileName.isEmpty()) {
        fileName = QUrl(sourceUrl).fileName();
        if (fileName.isEmpty())
            fileName = tr("Загрузка по ссылке");
    }

    Q_EMIT progress(tr("Яндекс качает %1").arg(fileName), -1);
    const bool ok = DiskApi::uploadFromUrlBlocking(m_token, joinPath(destDir, fileName), sourceUrl);
    Q_EMIT done(ok, ok ? tr("Загружено на Диск: %1").arg(fileName)
                       : tr("Не удалось загрузить по ссылке"));
    if (ok)
        Q_EMIT treeChanged();
}

void FileOpsWorker::createFolder(const QString& path)
{
    const bool ok = DiskApi::mkdirBlocking(m_token, path);
    Q_EMIT done(ok, ok ? tr("Папка создана") : tr("Не удалось создать папку"));
    if (ok)
        Q_EMIT treeChanged();
}

void FileOpsWorker::rename(const QString& from, const QString& to)
{
    const bool ok = DiskApi::moveBlocking(m_token, from, to);
    if (ok) {
        MetadataStore store;
        if (store.open(paths::databaseFile()))
            store.rename(from, to);
    }
    Q_EMIT done(ok, ok ? tr("Переименовано") : tr("Не удалось переименовать"));
    Q_EMIT treeChanged();
}

void FileOpsWorker::moveTo(const QStringList& paths, const QString& destDir)
{
    MetadataStore store;
    const bool haveStore = store.open(paths::databaseFile());

    int moved = 0;
    for (const QString& path : paths) {
        const QString target = joinPath(destDir, path.mid(path.lastIndexOf(QLatin1Char('/')) + 1));
        if (target == path)
            continue;
        if (DiskApi::moveBlocking(m_token, path, target)) {
            if (haveStore)
                store.rename(path, target);
            ++moved;
        }
    }

    Q_EMIT done(moved == paths.size(),
                moved == paths.size() ? tr("Перемещено: %1").arg(moved)
                                      : tr("Перемещено %1 из %2").arg(moved).arg(paths.size()));
    Q_EMIT treeChanged();
}

void FileOpsWorker::removeToTrash(const QString& path)
{
    const bool ok = DiskApi::removeBlocking(m_token, path, /*permanently=*/false);
    if (ok) {
        MetadataStore store;
        if (store.open(paths::databaseFile()))
            store.removeRecursive(path);
    }
    Q_EMIT done(ok, ok ? tr("Перемещено в корзину") : tr("Не удалось удалить"));
    Q_EMIT treeChanged();
}

void FileOpsWorker::duplicate(const QString& path)
{
    const QString target = naming::duplicateName(path);

    Q_EMIT progress(tr("Копирование %1").arg(path.mid(path.lastIndexOf(QLatin1Char('/')) + 1)), -1);
    const bool ok = DiskApi::copyBlocking(m_token, path, target);
    Q_EMIT done(ok, ok ? tr("Создана копия") : tr("Не удалось скопировать"));
    if (ok)
        Q_EMIT treeChanged();
}

void FileOpsWorker::publish(const QString& path, const QString& password, qint64 availableUntil)
{
    const QString url = DiskApi::publishBlocking(m_token, path, password, availableUntil);
    if (url.isEmpty()) {
        Q_EMIT done(false, tr("Не удалось опубликовать"));
        return;
    }

    MetadataStore store;
    if (store.open(paths::databaseFile()))
        store.setPublicUrl(path, url);

    Q_EMIT published(path, url);
    Q_EMIT done(true, tr("Ссылка скопирована: %1").arg(url));
    Q_EMIT treeChanged();
}

void FileOpsWorker::unpublish(const QString& path)
{
    const bool ok = DiskApi::unpublishBlocking(m_token, path);
    if (ok) {
        MetadataStore store;
        if (store.open(paths::databaseFile()))
            store.setPublicUrl(path, QString());
    }
    Q_EMIT done(ok, ok ? tr("Доступ по ссылке закрыт") : tr("Не удалось закрыть доступ"));
    Q_EMIT treeChanged();
}

void FileOpsWorker::pin(const QString& path)
{
    MetadataStore store;
    if (!store.open(paths::databaseFile())) {
        Q_EMIT done(false, store.lastError());
        return;
    }

    CacheManager cache(paths::cacheDir(), &store);
    const QString token = m_token;
    cache.setFetcher([this, token](const QString& remote, const QString& dest,
                                   const ContentProvider::ProgressFn&) {
        return DiskApi::downloadBlocking(token, remote, dest,
                                         [this, remote](qint64 received, qint64 total) {
                                             Q_EMIT progress(tr("Загрузка %1").arg(remote),
                                                             total > 0
                                                                 ? static_cast<qreal>(received) / total
                                                                 : -1);
                                         });
    });

    const bool ok = cache.pin(path);
    Q_EMIT done(ok, ok ? tr("Файл доступен офлайн") : tr("Не удалось загрузить файл"));
    Q_EMIT treeChanged();
}

void FileOpsWorker::unpin(const QString& path)
{
    MetadataStore store;
    if (!store.open(paths::databaseFile())) {
        Q_EMIT done(false, store.lastError());
        return;
    }

    CacheManager cache(paths::cacheDir(), &store);
    // Файл снова становится заглушкой.
    const bool ok = cache.evict(path);
    Q_EMIT done(ok, ok ? tr("Файл убран с устройства") : tr("Не удалось освободить место"));
    Q_EMIT treeChanged();
}

void FileOpsWorker::retryPending()
{
    MetadataStore store;
    if (!store.open(paths::databaseFile()))
        return;

    // Две очереди: правки в смонтированной папке и сорвавшиеся отправки.
    const QVector<QString> pending = store.pathsWithState(ContentState::Dirty);
    CacheManager cache(paths::cacheDir(), &store);

    int sent = 0;
    int failed = 0;
    for (const QString& path : pending) {
        const QFileInfo local(cache.localPathFor(path));
        if (!local.exists()) {
            // Заливать нечего; без сброса пометки файл числился бы вечно.
            store.setState(path, ContentState::Placeholder);
            continue;
        }

        // Только что изменённый скорее всего ещё пишут — отправим половину.
        if (local.lastModified().secsTo(QDateTime::currentDateTime()) < 30)
            continue;

        // Иначе дозагрузка молча затрёт правку с другого устройства.
        QString target = path;
        const auto known = store.get(path);
        if (known && !known->md5.isEmpty()) {
            Resource remote;
            if (DiskApi::statBlocking(m_token, path, &remote) && !remote.md5.isEmpty()
                && remote.md5 != known->md5) {
                target = naming::conflictName(path);
            }
        }

        Q_EMIT progress(tr("Дозагрузка %1").arg(local.fileName()), -1);
        if (DiskApi::uploadBlocking(m_token, target, local.absoluteFilePath())) {
            if (target != path) {
                store.setState(path, ContentState::Placeholder);
                Q_EMIT done(true, tr("Файл изменён и на Диске — ваша версия сохранена как «%1»")
                                      .arg(target.mid(target.lastIndexOf(QLatin1Char('/')) + 1)));
            } else {
                store.setSize(path, local.size());
                store.setState(path, ContentState::Cached);
            }
            ++sent;
        } else {
            ++failed;
        }
    }

    // Вторая очередь: файлы с компьютера, чья отправка сорвалась.
    for (const auto& item : store.pendingUploads()) {
        const QFileInfo source(item.localPath);
        if (!source.exists()) {
            // Источник исчез — повторять нечего.
            store.removePendingUpload(item.localPath);
            continue;
        }

        // Десять неудач подряд — дело не в связи; оставляем запись на глаза.
        if (item.attempts >= 10)
            continue;

        Q_EMIT progress(tr("Повторная отправка %1").arg(source.fileName()), -1);
        if (DiskApi::uploadBlocking(m_token, item.remotePath, item.localPath)) {
            store.removePendingUpload(item.localPath);
            ++sent;
        } else {
            store.notePendingUploadAttempt(item.localPath);
            ++failed;
        }
    }

    if (sent > 0 || failed > 0) {
        Q_EMIT done(failed == 0,
                    failed == 0 ? tr("Дозагружено файлов: %1").arg(sent)
                                : tr("Не удалось выгрузить: %1").arg(failed));
        Q_EMIT treeChanged();
    }
}

void FileOpsWorker::loadTrash()
{
    QVector<Resource> items;
    if (!DiskApi::trashListBlocking(m_token, &items)) {
        Q_EMIT done(false, tr("Не удалось прочитать корзину"));
        return;
    }
    Q_EMIT trashLoaded(items);
}

void FileOpsWorker::restoreFromTrash(const QString& trashPath)
{
    const bool ok = DiskApi::trashRestoreBlocking(m_token, trashPath);
    Q_EMIT done(ok, ok ? tr("Восстановлено") : tr("Не удалось восстановить"));
    if (ok) {
        loadTrash();
        Q_EMIT treeChanged();
    }
}

void FileOpsWorker::deleteFromTrash(const QString& trashPath)
{
    const bool ok = DiskApi::trashDeleteBlocking(m_token, trashPath);
    Q_EMIT done(ok, ok ? tr("Удалено безвозвратно") : tr("Не удалось удалить"));
    if (ok)
        loadTrash();
}

void FileOpsWorker::emptyTrash()
{
    const bool ok = DiskApi::trashDeleteBlocking(m_token, QString());
    Q_EMIT done(ok, ok ? tr("Корзина очищена") : tr("Не удалось очистить корзину"));
    if (ok)
        loadTrash();
}

} // namespace orbita
