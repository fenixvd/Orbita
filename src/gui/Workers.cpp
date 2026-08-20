#include "Workers.h"

#include "orbita/CacheManager.h"
#include "orbita/DiskApi.h"
#include "orbita/MetadataStore.h"
#include "orbita/Paths.h"
#include "orbita/RemoteOps.h"

#ifdef ORBITA_WITH_FUSE
#include "orbita/FuseMount.h"
#endif

#include <QDir>
#include <QEventLoop>
#include <QQueue>

namespace orbita {
namespace {

struct Page {
    QVector<Resource> items;
    bool hasMore = false;
    bool ok = false;
    QString error;
};

Page listPage(DiskApi& api, const QString& path, int offset)
{
    Page page;
    QEventLoop loop;
    auto c1 = QObject::connect(&api, &DiskApi::listed,
                               [&](const QString&, const QVector<Resource>& items, bool hasMore) {
                                   page.items = items;
                                   page.hasMore = hasMore;
                                   page.ok = true;
                                   loop.quit();
                               });
    auto c2 = QObject::connect(&api, &DiskApi::errorOccurred, [&](const QString& e) {
        page.error = e;
        loop.quit();
    });
    api.list(path, 200, offset);
    loop.exec();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    return page;
}

} // namespace

void SyncWorker::run(const QString& token, const QString& root)
{
    m_cancelled = false;

    MetadataStore store;
    if (!store.open(paths::databaseFile())) {
        Q_EMIT finished(false, store.lastError());
        return;
    }

    DiskApi api;
    api.setToken(token);

    QQueue<QString> pending;
    pending.enqueue(root);
    int dirs = 0;
    int files = 0;

    while (!pending.isEmpty()) {
        if (m_cancelled) {
            Q_EMIT finished(false, tr("Обход отменён"));
            return;
        }

        const QString dir = pending.dequeue();
        QVector<QString> seen;
        int offset = 0;
        while (true) {
            const Page page = listPage(api, dir, offset);
            if (!page.ok) {
                Q_EMIT finished(false, page.error);
                return;
            }
            store.upsertBatch(page.items);

            for (const Resource& r : page.items) {
                seen.append(r.path);
                if (r.isDir) {
                    ++dirs;
                    pending.enqueue(r.path);
                } else {
                    ++files;
                }
            }
            if (!page.hasMore)
                break;
            offset += page.items.size();
        }

        // Убираем из папки то, чего на Диске больше нет: обход обновляет
        // существующее, но сам по себе ничего не удаляет.
        store.pruneMissing(dir, seen);
        Q_EMIT progress(dirs, files);
    }

    Q_EMIT finished(true, {});
}

void SyncWorker::pollChanges(const QString& token)
{
    QVector<Resource> items;
    if (!DiskApi::lastUploadedBlocking(token, &items) || items.isEmpty())
        return;

    MetadataStore store;
    if (!store.open(paths::databaseFile()))
        return;

    // Считаем изменением только то, чего у нас нет или что изменилось:
    // иначе сигнал летел бы на каждый опрос и списки моргали бы впустую.
    QVector<Resource> fresh;
    for (const Resource& item : items) {
        const auto known = store.get(item.path);
        if (!known || known->md5 != item.md5 || known->size != item.size)
            fresh.append(item);
    }
    if (fresh.isEmpty())
        return;

    store.upsertBatch(fresh);
    Q_EMIT treeChanged();
}

void SyncWorker::refreshDir(const QString& token, const QString& path)
{
    MetadataStore store;
    if (!store.open(paths::databaseFile()))
        return;

    DiskApi api;
    api.setToken(token);

    QVector<QString> seen;
    int offset = 0;
    while (true) {
        const Page page = listPage(api, path, offset);
        if (!page.ok)
            return;

        store.upsertBatch(page.items);
        for (const Resource& r : page.items)
            seen.append(r.path);

        if (!page.hasMore)
            break;
        offset += page.items.size();
    }

    // Всё, чего в свежем списке не оказалось, с Диска исчезло.
    const int removed = store.pruneMissing(path, seen);
    if (removed > 0 || !seen.isEmpty())
        Q_EMIT treeChanged();
}

void SyncWorker::fetchCapacity(const QString& token)
{
    qint64 total = 0;
    qint64 used = 0;
    if (DiskApi::capacityBlocking(token, &total, &used))
        Q_EMIT capacity(total, used);
}

void MountWorker::requestStop()
{
#ifdef ORBITA_WITH_FUSE
    if (FuseMount* mount = m_mount.load())
        mount->stop();
#endif
}

void MountWorker::run(const QString& token, const QString& mountPoint)
{
#ifdef ORBITA_WITH_FUSE
    MetadataStore store;
    if (!store.open(paths::databaseFile())) {
        Q_EMIT stopped(store.lastError());
        return;
    }

    CacheManager cache(paths::cacheDir(), &store);
    cache.setBudget(paths::cacheBudget());
    cache.setFetcher([token](const QString& remote, const QString& dest,
                             const ContentProvider::ProgressFn& progress) {
        return DiskApi::downloadBlocking(token, remote, dest, progress);
    });
    cache.setRangeFetcher([token](const QString& remote, qint64 offset, qint64 size, char* buffer) {
        return DiskApi::readRangeBlocking(token, remote, offset, size, buffer);
    });

    DiskOps ops(token);
    QDir().mkpath(mountPoint);

    Q_EMIT mounted();

    FuseMount mount(store, cache, &ops);
    m_mount.store(&mount);
    const bool ok = mount.run(mountPoint);
    m_mount.store(nullptr);

    Q_EMIT stopped(ok ? QString() : mount.lastError());
#else
    Q_UNUSED(token);
    Q_UNUSED(mountPoint);
    Q_EMIT stopped(tr("Сборка без поддержки FUSE"));
#endif
}

} // namespace orbita
