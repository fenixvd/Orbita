#include "orbita/CacheManager.h"
#include "orbita/MetadataStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace orbita;

/// Кэш содержимого: именно он отвечает за обещание «файлы не занимают места».
/// Сеть подменяем — проверяем поведение, а не Яндекс.
class TestCacheManager : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    static Resource file(const QString& path, qint64 size)
    {
        Resource r;
        r.path = path;
        r.name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
        r.size = size;
        r.modified = QDateTime::currentDateTimeUtc();
        return r;
    }

private Q_SLOTS:
    void materializeDownloadsOnceAndMarksCached()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("once.db"))));
        store.upsert(file(QStringLiteral("/файл.txt"), 6));

        CacheManager cache(m_dir.filePath(QStringLiteral("once")), &store);
        int downloads = 0;
        cache.setFetcher([&downloads](const QString&, const QString& dest,
                                      const ContentProvider::ProgressFn&) {
            ++downloads;
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("данные");
            return true;
        });

        const QString first = cache.materialize(QStringLiteral("/файл.txt"));
        QVERIFY(!first.isEmpty());
        QCOMPARE(store.get(QStringLiteral("/файл.txt"))->state, ContentState::Cached);

        // Повторное обращение обязано брать из кэша, а не качать снова.
        cache.materialize(QStringLiteral("/файл.txt"));
        QCOMPARE(downloads, 1);
    }

    void failedDownloadLeavesNothingBehind()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("fail.db"))));
        store.upsert(file(QStringLiteral("/битый.bin"), 10));

        CacheManager cache(m_dir.filePath(QStringLiteral("fail")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            // Оборвались на середине.
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("полов");
            return false;
        });

        QVERIFY(cache.materialize(QStringLiteral("/битый.bin")).isEmpty());
        // Недокачанное не должно выглядеть готовым файлом.
        QVERIFY(!cache.isMaterialized(QStringLiteral("/битый.bin")));
        QCOMPARE(store.get(QStringLiteral("/битый.bin"))->state, ContentState::Placeholder);
    }

    void readRangeUsesNetworkWhenFileIsNotLocal()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("range.db"))));
        store.upsert(file(QStringLiteral("/большой.iso"), 1000000));

        CacheManager cache(m_dir.filePath(QStringLiteral("range")), &store);
        qint64 askedOffset = -1;
        cache.setRangeFetcher([&askedOffset](const QString&, qint64 offset, qint64 size,
                                             char* buffer) {
            askedOffset = offset;
            memset(buffer, 'X', size);
            return size;
        });
        bool wholeFileRequested = false;
        cache.setFetcher([&wholeFileRequested](const QString&, const QString&,
                                               const ContentProvider::ProgressFn&) {
            wholeFileRequested = true;
            return false;
        });

        char buffer[16] = {};
        const qint64 read = cache.readRange(QStringLiteral("/большой.iso"), 4096, 16, buffer);

        // Полная загрузка при чтении куска — та самая ошибка, из-за которой
        // файловый менеджер тянул гигабайты ради нескольких байт.
        QVERIFY(!wholeFileRequested);
        QCOMPARE(read, 16);
        QCOMPARE(askedOffset, 4096);
        QCOMPARE(buffer[0], 'X');
        // Ничего не осело на диск.
        QVERIFY(!cache.isMaterialized(QStringLiteral("/большой.iso")));
    }

    void budgetEvictsOldButKeepsPinned()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("budget.db"))));
        store.upsertBatch({ file(QStringLiteral("/закреплён.bin"), 4096),
                            file(QStringLiteral("/обычный.bin"), 4096) });

        CacheManager cache(m_dir.filePath(QStringLiteral("budget")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(4096, 'z'));
            return true;
        });

        QVERIFY(cache.pin(QStringLiteral("/закреплён.bin")));
        QVERIFY(!cache.materialize(QStringLiteral("/обычный.bin")).isEmpty());

        // Места хватает лишь на один файл — уйти должен незакреплённый.
        cache.setBudget(5000);
        QVERIFY(cache.enforceBudget() > 0);

        QVERIFY(cache.isMaterialized(QStringLiteral("/закреплён.bin")));
        QVERIFY(!cache.isMaterialized(QStringLiteral("/обычный.bin")));
    }

    void evictTurnsFileBackIntoPlaceholder()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("evict.db"))));
        store.upsert(file(QStringLiteral("/убрать.bin"), 3));

        CacheManager cache(m_dir.filePath(QStringLiteral("evict")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("abc");
            return true;
        });

        cache.materialize(QStringLiteral("/убрать.bin"));
        QVERIFY(cache.evict(QStringLiteral("/убрать.bin")));

        QVERIFY(!cache.isMaterialized(QStringLiteral("/убрать.bin")));
        QCOMPARE(store.get(QStringLiteral("/убрать.bin"))->state, ContentState::Placeholder);
    }

    void brokenDownloadIsRejectedByChecksum()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("md5.db"))));

        Resource r = file(QStringLiteral("/важный.bin"), 3);
        // md5 от «abc», а скачается другое — значит закачка испортилась.
        r.md5 = QStringLiteral("900150983cd24fb0d6963f7d28e17f72");
        store.upsert(r);

        CacheManager cache(m_dir.filePath(QStringLiteral("md5")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("ab");   // оборвалось на середине
            return true;     // а HTTP счёл это успехом
        });

        QVERIFY(cache.materialize(QStringLiteral("/важный.bin")).isEmpty());
        QVERIFY(!cache.isMaterialized(QStringLiteral("/важный.bin")));
    }

    void goodDownloadPassesChecksum()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("md5ok.db"))));

        Resource r = file(QStringLiteral("/целый.bin"), 3);
        r.md5 = QStringLiteral("900150983cd24fb0d6963f7d28e17f72"); // md5 от «abc»
        store.upsert(r);

        CacheManager cache(m_dir.filePath(QStringLiteral("md5ok")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("abc");
            return true;
        });

        QVERIFY(!cache.materialize(QStringLiteral("/целый.bin")).isEmpty());
        QVERIFY(cache.isMaterialized(QStringLiteral("/целый.bin")));
    }

    void renameLocalFollowsTheFile()
    {
        MetadataStore store;
        QVERIFY(store.open(m_dir.filePath(QStringLiteral("rename.db"))));
        store.upsert(file(QStringLiteral("/было.txt"), 3));

        CacheManager cache(m_dir.filePath(QStringLiteral("rename")), &store);
        cache.setFetcher([](const QString&, const QString& dest,
                            const ContentProvider::ProgressFn&) {
            QFile f(dest);
            f.open(QIODevice::WriteOnly);
            f.write("abc");
            return true;
        });

        cache.materialize(QStringLiteral("/было.txt"));
        QVERIFY(cache.renameLocal(QStringLiteral("/было.txt"), QStringLiteral("/стало.txt")));

        // Скачанное не должно теряться из-за переименования.
        QVERIFY(cache.isMaterialized(QStringLiteral("/стало.txt")));
        QVERIFY(!cache.isMaterialized(QStringLiteral("/было.txt")));
    }
};

QTEST_GUILESS_MAIN(TestCacheManager)
#include "tst_cachemanager.moc"
