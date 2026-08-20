#include "TrayIcon.h"

#include "AppController.h"
#include "Settings.h"

#include <KStatusNotifierItem>
#include <kstatusnotifieritem_version.h>

#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QWindow>

namespace orbita {

TrayIcon::TrayIcon(AppController* controller, Settings* settings, QWindow* window, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_settings(settings)
    , m_window(window)
{
    connect(m_settings, &Settings::trayEnabledChanged, this, &TrayIcon::applySettings);
    applySettings();
}

void TrayIcon::applySettings()
{
    if (m_settings->trayEnabled() && !m_item)
        createItem();
    else if (!m_settings->trayEnabled() && m_item)
        destroyItem();
}

void TrayIcon::destroyItem()
{
    // Только удалением: «скрытого» состояния протокол не знает.
    delete m_item;
    m_item = nullptr;

    // Без значка окно — единственный способ управления, крестик снова закрывает.
    QGuiApplication::setQuitOnLastWindowClosed(true);
    if (m_window)
        m_window->show();
}

void TrayIcon::createItem()
{
    QGuiApplication::setQuitOnLastWindowClosed(false);

    m_item = new KStatusNotifierItem(QStringLiteral("ru.rainedev.orbita"), this);
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setTitle(QStringLiteral("Orbita"));
    m_item->setIconByName(QStringLiteral("ru.rainedev.orbita"));
    // Только Active: Passive KDE прячет в выпадающий список.
    m_item->setStatus(KStatusNotifierItem::Active);

    // Меню открывается и левой кнопкой. Показывает его сама панель, а не мы:
    // в Wayland приложение не знает экранных координат щелчка, и меню,
    // выставленное вручную, уезжало в сторону от значка.
    //
    // Метод появился в KF6 6.14; в Debian 13 идёт 6.13, и там левая кнопка
    // просто прячет и показывает окно — как было по умолчанию.
#if KSTATUSNOTIFIERITEM_VERSION_MAJOR > 6 \
    || (KSTATUSNOTIFIERITEM_VERSION_MAJOR == 6 && KSTATUSNOTIFIERITEM_VERSION_MINOR >= 14)
    m_item->setIsMenu(true);
#endif

    QMenu* menu = m_item->contextMenu();

    auto* showAction = new QAction(QIcon::fromTheme(QStringLiteral("window")),
                                   tr("Показать окно"), menu);
    connect(showAction, &QAction::triggered, this, [this] {
        if (!m_window)
            return;
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
    });
    menu->addAction(showAction);

    menu->addSeparator();

    m_mountAction = new QAction(menu);
    connect(m_mountAction, &QAction::triggered, this, [this] {
        if (m_controller->mounted())
            m_controller->unmount();
        else
            m_controller->mount();
    });
    menu->addAction(m_mountAction);

    m_openAction = new QAction(QIcon::fromTheme(QStringLiteral("folder-open")),
                               tr("Открыть папку Диска"), menu);
    connect(m_openAction, &QAction::triggered, this,
            [this] { m_controller->openMountPoint(); });
    menu->addAction(m_openAction);

    auto* syncAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                   tr("Обновить дерево"), menu);
    connect(syncAction, &QAction::triggered, this, [this] { m_controller->startSync(); });
    menu->addAction(syncAction);

    menu->addSeparator();

    auto* quitAction = new QAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                   tr("Выйти из Orbita"), menu);
    // Крестик Диск не отключает — выйти по-настоящему можно только отсюда.
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    menu->addAction(quitAction);

    connect(m_controller, &AppController::mountedChanged, this, &TrayIcon::updateState);
    connect(m_controller, &AppController::statusChanged, this, &TrayIcon::updateState);
    connect(m_controller, &AppController::spaceChanged, this, &TrayIcon::updateState);
    updateState();
}

void TrayIcon::updateState()
{
    if (!m_item)
        return;

    const bool mounted = m_controller->mounted();
    if (m_mountAction) {
        m_mountAction->setText(mounted ? tr("Отключить Диск") : tr("Подключить Диск"));
        m_mountAction->setIcon(QIcon::fromTheme(mounted ? QStringLiteral("media-eject")
                                                        : QStringLiteral("drive-harddisk")));
    }
    if (m_openAction)
        m_openAction->setEnabled(mounted);

    // Подсказка отвечает на единственный вопрос: подключён Диск или нет.
    QString tip = mounted ? tr("Диск подключён") : tr("Диск отключён");
    if (!m_controller->spaceText().isEmpty())
        tip += QStringLiteral(" · ") + m_controller->spaceText();

    m_item->setToolTip(QStringLiteral("ru.rainedev.orbita"), QStringLiteral("Orbita"), tip);
}

} // namespace orbita
