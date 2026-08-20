#include "orbita/DiskApi.h"
#include "orbita/Naming.h"

#include <QTest>

using namespace orbita;

/// Правила именования и разбора путей — мелочь, на которой уже обжигались:
/// из-за префикса «trash:» операции над Корзиной молча не работали.
class TestNaming : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void suffixGoesBeforeExtension()
    {
        QCOMPARE(naming::withSuffix(QStringLiteral("/Фото/кот.jpg"), QStringLiteral(" (копия)")),
                 QStringLiteral("/Фото/кот (копия).jpg"));
    }

    void suffixForFileWithoutExtension()
    {
        QCOMPARE(naming::withSuffix(QStringLiteral("/Документы/договор"), QStringLiteral(" (копия)")),
                 QStringLiteral("/Документы/договор (копия)"));
    }

    void dotInFolderNameIsNotExtension()
    {
        // Точка выше по пути не должна приниматься за расширение файла.
        QCOMPARE(naming::withSuffix(QStringLiteral("/Архив.2026/отчёт"), QStringLiteral(" (копия)")),
                 QStringLiteral("/Архив.2026/отчёт (копия)"));
    }

    void hiddenFileKeepsItsName()
    {
        // «.bashrc» — это имя целиком, а не расширение «bashrc».
        QCOMPARE(naming::withSuffix(QStringLiteral("/Настройки/.bashrc"), QStringLiteral(" (копия)")),
                 QStringLiteral("/Настройки/.bashrc (копия)"));
    }

    void conflictNameCarriesTime()
    {
        const QDateTime when(QDate(2026, 8, 17), QTime(2, 30));
        QCOMPARE(naming::conflictName(QStringLiteral("/заметка.txt"), when),
                 QStringLiteral("/заметка (конфликт 2026-08-17 02-30).txt"));
    }

    void normalizeStripsDiskPrefix()
    {
        QCOMPARE(DiskApi::normalizePath(QStringLiteral("disk:/Фото/кот.jpg")),
                 QStringLiteral("/Фото/кот.jpg"));
    }

    void normalizeStripsTrashPrefix()
    {
        // API отдаёт пути Корзины с префиксом, а принимает обратно без него.
        QCOMPARE(DiskApi::normalizePath(QStringLiteral("trash:/файл_abc123")),
                 QStringLiteral("/файл_abc123"));
    }

    void normalizeEmptyPathBecomesRoot()
    {
        QCOMPARE(DiskApi::normalizePath(QStringLiteral("disk:")), QStringLiteral("/"));
    }
};

QTEST_GUILESS_MAIN(TestNaming)
#include "tst_naming.moc"
