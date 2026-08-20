#include "AppController.h"
#include "Settings.h"
#include "SingleInstance.h"
#include "ThumbnailProvider.h"
#include "TrayIcon.h"

#include <QLocale>
#include <QTimer>
#include <QTranslator>

// QApplication, а не QGuiApplication: значок в лотке строит меню на QMenu,
// а это виджеты. Само окно при этом остаётся на QML.
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QWindow>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Orbita"));
    QCoreApplication::setOrganizationName(QStringLiteral("Orbita"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ORBITA_VERSION));

    // В Wayland значок окна берётся из .desktop-файла, а не из setWindowIcon,
    // поэтому имя файла обязано совпадать с установленным.
    QGuiApplication::setDesktopFileName(QStringLiteral("ru.rainedev.orbita"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("ru.rainedev.orbita")));

    // Исходные строки русские; для остальных языков подставляем перевод.
    // Для русской локали файла нет — и не нужно.
    QTranslator translator;
    const QStringList translationDirs {
        QStringLiteral(ORBITA_TRANSLATIONS_DIR),
        QCoreApplication::applicationDirPath(), // сборочный каталог при разработке
        QStringLiteral(":/i18n"),
    };
    for (const QString& dir : translationDirs) {
        if (translator.load(QLocale(), QStringLiteral("orbita"), QStringLiteral("_"), dir)) {
            QCoreApplication::installTranslator(&translator);
            break;
        }
    }

    // Закрытие окна не завершает работу: Диск смонтирован ровно столько,
    // сколько живёт процесс, и окно ему не хозяин.
    QApplication::setQuitOnLastWindowClosed(false);

    // Kirigami рассчитан на стиль рабочего стола; без него элементы
    // выглядят чужеродно в KDE.
    if (QQuickStyle::name().isEmpty())
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // Вторая копия смонтировала бы Диск поверх первой — вместо этого
    // просим уже работающую показать окно и выходим.
    orbita::SingleInstance instance;
    if (!instance.takeOwnership())
        return 0;

    QQmlApplicationEngine engine;
    // Миниатюры берутся у Яндекса по ссылке, без загрузки самих файлов.
    engine.addImageProvider(QStringLiteral("orbita"), new orbita::ThumbnailProvider);
    engine.loadFromModule("Orbita", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    auto* controller = engine.singletonInstance<orbita::AppController*>(
        QStringLiteral("Orbita"), QStringLiteral("AppController"));
    auto* settings = engine.singletonInstance<orbita::Settings*>(QStringLiteral("Orbita"),
                                                                 QStringLiteral("Settings"));
    auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
    instance.setWindow(window);

    if (controller && settings) {
        new orbita::TrayIcon(controller, settings, window, &app);

        // Подключение после запуска очереди событий: до неё рабочие потоки
        // ещё не готовы принимать задания.
        if (settings->automount())
            QTimer::singleShot(0, controller, &orbita::AppController::mount);
    }

    return app.exec();
}
