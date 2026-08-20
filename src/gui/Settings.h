#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>

namespace orbita {

/// Настройки приложения.
///
/// Хранятся в ~/.config/Orbita/Orbita.conf рядом с остальным нашим добром.
class Settings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool trayEnabled READ trayEnabled WRITE setTrayEnabled NOTIFY trayEnabledChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(bool automount READ automount WRITE setAutomount NOTIFY automountChanged)
    Q_PROPERTY(QString mountPoint READ mountPoint WRITE setMountPoint NOTIFY mountPointChanged)
    Q_PROPERTY(QString uploadFolder READ uploadFolder WRITE setUploadFolder
                   NOTIFY uploadFolderChanged)
    /// Потолок кэша в гигабайтах — в окне удобнее целыми числами.
    Q_PROPERTY(int cacheBudgetGiB READ cacheBudgetGiB WRITE setCacheBudgetGiB
                   NOTIFY cacheBudgetChanged)
    Q_PROPERTY(int pollSeconds READ pollSeconds WRITE setPollSeconds NOTIFY pollSecondsChanged)
    Q_PROPERTY(QString cacheSizeText READ cacheSizeText NOTIFY cacheChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    bool trayEnabled() const;
    void setTrayEnabled(bool enabled);

    /// Запуск вместе с сеансом. Хранится не в конфиге, а наличием файла
    /// в ~/.config/autostart — так это видят и системные настройки KDE.
    bool autostart() const;
    void setAutostart(bool enabled);

    /// Подключать Диск сразу при запуске.
    bool automount() const;
    void setAutomount(bool enabled);

    /// Смена точки подключения применяется при следующем подключении Диска:
    /// переносить работающую файловую систему на ходу нельзя.
    QString mountPoint() const;
    void setMountPoint(const QString& path);

    QString uploadFolder() const;
    void setUploadFolder(const QString& path);

    int cacheBudgetGiB() const;
    void setCacheBudgetGiB(int gib);

    int pollSeconds() const;
    void setPollSeconds(int seconds);

    QString cacheSizeText() const;

    /// Удаляет скачанное содержимое и миниатюры. Закреплённое офлайн
    /// придётся качать заново — предупредить пользователя обязательно.
    Q_INVOKABLE void clearCache();

Q_SIGNALS:
    void trayEnabledChanged();
    void autostartChanged();
    void automountChanged();
    void mountPointChanged();
    void uploadFolderChanged();
    void cacheBudgetChanged();
    void pollSecondsChanged();
    void cacheChanged();

private:
    QString autostartFile() const;

    QSettings m_settings;
};

} // namespace orbita
