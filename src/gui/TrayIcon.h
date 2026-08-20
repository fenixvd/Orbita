#pragma once

#include <QObject>

class KStatusNotifierItem;
class QAction;
class QWindow;

namespace orbita {

class AppController;
class Settings;

/// Значок в системном лотке.
///
/// Через KStatusNotifierItem, а не QSystemTrayIcon: в Wayland значок живёт
/// по протоколу StatusNotifierItem, и старый способ там работает лишь
/// через прослойку, а в части оболочек не показывается вовсе.
///
/// Смысл значка для этого приложения особый: Диск смонтирован ровно столько,
/// сколько живёт процесс. Пока значок есть, закрытие окна прячет его в лоток,
/// иначе вместе с окном отвалилась бы файловая система.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    TrayIcon(AppController* controller, Settings* settings, QWindow* window,
             QObject* parent = nullptr);

private:
    void applySettings();
    void createItem();
    void destroyItem();
    void updateState();

    AppController* m_controller = nullptr;
    Settings* m_settings = nullptr;
    QWindow* m_window = nullptr;
    KStatusNotifierItem* m_item = nullptr;
    QAction* m_mountAction = nullptr;
    QAction* m_openAction = nullptr;
};

} // namespace orbita
