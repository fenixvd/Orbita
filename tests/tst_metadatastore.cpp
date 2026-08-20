#include "orbita/MetadataStore.h"

#include <QTemporaryDir>
#include <QTest>

using namespace orbita;

/// Хранилище метаданных — сердце клиента: из него отвечает файловая система,
/// по нему строятся эмблемы и поиск. Ошибка здесь видна не сразу, поэтому
/// проверяем и обычные случаи, и те, на которых уже спотыкались.
class TestMetadataStore : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    MetadataStore m_store;

    static Resource file(const QString& path, qint64 size = 100,
                         ContentState state = ContentState::Placeholder,
                         const QString& md5 = {})
    {
        Resource r;
        r.path = path;
        r.name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
        r.isDir = false;
        r.size = size;
        r.state = state;
        r.md5 = md5;
        r.modified = QDateTime::currentDateTimeUtc();
        return r;
    }

    static Resource dir(const QString& path)
    {
        Resource r = file(path, 0);
        r.isDir = true;
        return r;
    }

private Q_SLOTS:
    void init()
    {
        // Своя база на каждую проверку: тесты не должны зависеть друг от друга.
        m_store.close();
        QVERIFY(m_store.open(m_dir.filePath(QStringLiteral("%1.db").arg(QTest::currentTestFunction()))));
    }

    void storesAndReadsBack()
    {
        QVERIFY(m_store.upsert(file(QStringLiteral("/Фото/кот.jpg"), 2048)));

        const auto found = m_store.get(QStringLiteral("/Фото/кот.jpg"));
        QVERIFY(found.has_value());
        QCOMPARE(found->name, QStringLiteral("кот.jpg"));
        QCOMPARE(found->size, 2048);
    }

    void childrenListDirsFirst()
    {
        m_store.upsertBatch({ file(QStringLiteral("/Папка/б.txt")),
                              dir(QStringLiteral("/Папка/вложенная")),
                              file(QStringLiteral("/Папка/а.txt")) });

        const auto children = m_store.children(QStringLiteral("/Папка"));
        QCOMPARE(children.size(), 3);
        QVERIFY(children.at(0).isDir);
        QCOMPARE(children.at(1).name, QStringLiteral("а.txt"));
    }

    void upsertKeepsContentState()
    {
        // Обход Диска обновляет метаданные, но не должен сбрасывать
        // закрепление офлайн — иначе файлы «отваливались» бы сами.
        m_store.upsert(file(QStringLiteral("/файл.bin"), 10));
        m_store.setState(QStringLiteral("/файл.bin"), ContentState::Pinned);
        m_store.upsert(file(QStringLiteral("/файл.bin"), 20));

        const auto found = m_store.get(QStringLiteral("/файл.bin"));
        QCOMPARE(found->size, 20);
        QCOMPARE(found->state, ContentState::Pinned);
    }

    void renameMovesWholeSubtree()
    {
        m_store.upsertBatch({ dir(QStringLiteral("/Старая")),
                              dir(QStringLiteral("/Старая/внутри")),
                              file(QStringLiteral("/Старая/внутри/файл.txt")) });

        QVERIFY(m_store.rename(QStringLiteral("/Старая"), QStringLiteral("/Новая")));

        QVERIFY(!m_store.get(QStringLiteral("/Старая/внутри/файл.txt")).has_value());
        const auto moved = m_store.get(QStringLiteral("/Новая/внутри/файл.txt"));
        QVERIFY(moved.has_value());
        // Имя файла при переименовании папки меняться не должно.
        QCOMPARE(moved->name, QStringLiteral("файл.txt"));

        const auto folder = m_store.get(QStringLiteral("/Новая"));
        QCOMPARE(folder->name, QStringLiteral("Новая"));
    }

    void pruneRemovesWhatServerNoLongerHas()
    {
        m_store.upsertBatch({ file(QStringLiteral("/Папка/остался.txt")),
                              file(QStringLiteral("/Папка/удалён.txt")),
                              dir(QStringLiteral("/Папка/подпапка")),
                              file(QStringLiteral("/Папка/подпапка/внутри.txt")) });

        // Сервер прислал только один файл — остальное исчезло с Диска.
        const int removed = m_store.pruneMissing(QStringLiteral("/Папка"),
                                                 { QStringLiteral("/Папка/остался.txt") });

        QCOMPARE(removed, 2);
        QVERIFY(m_store.get(QStringLiteral("/Папка/остался.txt")).has_value());
        QVERIFY(!m_store.get(QStringLiteral("/Папка/удалён.txt")).has_value());
        // Удалённая папка должна унести и своё содержимое.
        QVERIFY(!m_store.get(QStringLiteral("/Папка/подпапка/внутри.txt")).has_value());
    }

    void folderStatsCountWholeSubtree()
    {
        m_store.upsertBatch({
            file(QStringLiteral("/Проект/а.txt"), 1, ContentState::Cached),
            file(QStringLiteral("/Проект/б.txt"), 1, ContentState::Placeholder),
            file(QStringLiteral("/Проект/вложенная/в.txt"), 1, ContentState::Dirty),
            // Посторонние папки с похожим началом имени — в счёт попадать
            // не должны. Обе стороны важны: «-» идёт до «/», «0» и «X» —
            // после, и именно на второй границе легко ошибиться.
            file(QStringLiteral("/Проект-старый/г.txt"), 1, ContentState::Cached),
            file(QStringLiteral("/Проект0/д.txt"), 1, ContentState::Cached),
            file(QStringLiteral("/ПроектX/е.txt"), 1, ContentState::Cached),
        });

        const auto stats = m_store.folderStats(QStringLiteral("/Проект"));
        QCOMPARE(stats.files, 3);
        QCOMPARE(stats.onDevice, 2);
        QCOMPARE(stats.dirty, 1);
    }

    void searchFindsByPartOfName()
    {
        m_store.upsertBatch({ file(QStringLiteral("/Фото/закат.jpg")),
                              file(QStringLiteral("/Документы/договор.pdf")) });

        QCOMPARE(m_store.search(QStringLiteral("зака")).size(), 1);
        QCOMPARE(m_store.search(QStringLiteral("зака")).first().name, QStringLiteral("закат.jpg"));
    }

    void searchTreatsWildcardsAsText()
    {
        // Символы % и _ в поиске должны искаться буквально, а не как шаблон,
        // иначе запрос «%» вернул бы вообще всё.
        m_store.upsertBatch({ file(QStringLiteral("/скидка 50%.txt")),
                              file(QStringLiteral("/обычный.txt")) });

        QCOMPARE(m_store.search(QStringLiteral("%")).size(), 1);
    }

    void pendingUploadsSurviveAsQueue()
    {
        QVERIFY(m_store.addPendingUpload(QStringLiteral("/home/пользователь/файл.txt"),
                                         QStringLiteral("/Загрузки/файл.txt")));
        QCOMPARE(m_store.pendingUploads().size(), 1);

        m_store.notePendingUploadAttempt(QStringLiteral("/home/пользователь/файл.txt"));
        QCOMPARE(m_store.pendingUploads().first().attempts, 2);
        QCOMPARE(m_store.pendingUploads().first().remotePath, QStringLiteral("/Загрузки/файл.txt"));

        QVERIFY(m_store.removePendingUpload(QStringLiteral("/home/пользователь/файл.txt")));
        QVERIFY(m_store.pendingUploads().isEmpty());
    }

    void cachedBytesCountOnlyLocalContent()
    {
        m_store.upsertBatch({ file(QStringLiteral("/а"), 100, ContentState::Cached),
                              file(QStringLiteral("/б"), 200, ContentState::Pinned),
                              file(QStringLiteral("/в"), 400, ContentState::Placeholder) });

        QCOMPARE(m_store.cachedBytes(), 300);
    }
};

QTEST_GUILESS_MAIN(TestMetadataStore)
#include "tst_metadatastore.moc"
