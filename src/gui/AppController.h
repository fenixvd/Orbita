#pragma once

// Полное определение, а не объявление: свойство отдаёт указатель на модель,
// и системе типов QML нужен весь класс целиком.
#include "TrashModel.h"
#include "orbita/TokenStore.h"

#include <QObject>
#include <QQmlEngine>
#include <QThread>
#include <QTimer>

namespace orbita {

class AuthManager;
class SyncWorker;
class MountWorker;
class FileOpsWorker;

/// Связывает интерфейс с ядром: вход, обход дерева, монтирование.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool mounted READ mounted NOTIFY mountedChanged)
    Q_PROPERTY(bool waitingForCode READ waitingForCode NOTIFY waitingForCodeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    /// Ход текущей операции: от 0 до 1, либо -1 — когда доля неизвестна.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString mountPoint READ mountPoint CONSTANT)
    Q_PROPERTY(QString authUrl READ authUrl NOTIFY authUrlChanged)
    /// «Занято 12,3 ГБ из 1,0 ТБ» — готовая строка для интерфейса.
    Q_PROPERTY(QString spaceText READ spaceText NOTIFY spaceChanged)
    /// Доля занятого от 0 до 1 — для полоски заполнения.
    Q_PROPERTY(qreal spaceRatio READ spaceRatio NOTIFY spaceChanged)
    Q_PROPERTY(orbita::TrashModel* trash READ trash CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    /// Есть ли связь. Без неё бессмысленно опрашивать Диск и дёргать очередь.
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    bool loggedIn() const { return m_creds.isValid(); }
    bool busy() const { return m_busy; }
    bool mounted() const { return m_mounted; }
    bool waitingForCode() const { return m_waitingForCode; }
    QString status() const { return m_status; }
    qreal progress() const { return m_progress; }
    QString mountPoint() const;
    QString authUrl() const { return m_authUrl; }
    QString spaceText() const { return m_spaceText; }
    qreal spaceRatio() const;

    /// Открывает страницу согласия в браузере.
    Q_INVOKABLE void startLogin();
    /// Код, который Яндекс показал в браузере.
    Q_INVOKABLE void submitCode(const QString& code);
    Q_INVOKABLE void logout();

    Q_INVOKABLE void startSync();
    /// Обновляет сведения о занятом месте.
    Q_INVOKABLE void refreshCapacity();
    /// Перечитывает одну папку с Диска — зовётся при переходе по дереву.
    Q_INVOKABLE void refreshDir(const QString& path);
    Q_INVOKABLE void mount();
    Q_INVOKABLE void unmount();
    /// Открыть смонтированную папку в файловом менеджере.
    Q_INVOKABLE void openMountPoint();
    Q_INVOKABLE void copyToClipboard(const QString& text);
    /// Открыть файл в системном приложении. Работает только при подключённом
    /// Диске: путь ведёт внутрь смонтированной папки.
    Q_INVOKABLE void openFile(const QString& diskPath);

    // Операции над файлами. Все выполняются в отдельном потоке.
    Q_INVOKABLE void uploadUrls(const QList<QUrl>& urls, const QString& destDir);
    /// Скачивание на Диск по ссылке силами Яндекса.
    Q_INVOKABLE void uploadFromUrl(const QString& sourceUrl,
                                   const QString& destDir,
                                   const QString& name = {});
    Q_INVOKABLE void createFolder(const QString& parentDir, const QString& name);
    Q_INVOKABLE void renameItem(const QString& path, const QString& newName);
    Q_INVOKABLE void moveItems(const QStringList& paths, const QString& destDir);
    Q_INVOKABLE void removeItem(const QString& path);
    // Пакетные действия: выделить десяток файлов и повторять по одному —
    // не работа.
    Q_INVOKABLE void removeItems(const QStringList& paths);
    Q_INVOKABLE void pinItems(const QStringList& paths);
    Q_INVOKABLE void unpinItems(const QStringList& paths);
    Q_INVOKABLE void duplicateItem(const QString& path);
    /// Пустой пароль и нулевой срок дают обычную бессрочную ссылку.
    Q_INVOKABLE void publishItem(const QString& path,
                                 const QString& password = {},
                                 int availableDays = 0);
    Q_INVOKABLE void unpublishItem(const QString& path);
    Q_INVOKABLE void pinItem(const QString& path);
    Q_INVOKABLE void unpinItem(const QString& path);

    // Корзина.
    Q_INVOKABLE void loadTrash();
    Q_INVOKABLE void restoreFromTrash(const QString& trashPath);
    Q_INVOKABLE void deleteFromTrash(const QString& trashPath);
    Q_INVOKABLE void emptyTrash();

    TrashModel* trash() const { return m_trash; }
    QString version() const { return QStringLiteral(ORBITA_VERSION); }
    bool online() const { return m_online; }

Q_SIGNALS:
    void loggedInChanged();
    void busyChanged();
    void mountedChanged();
    void waitingForCodeChanged();
    void statusChanged();
    void onlineChanged();
    void progressChanged();
    void authUrlChanged();
    void spaceChanged();
    void errorOccurred(const QString& message);
    /// Дерево изменилось — спискам пора обновиться.
    void treeChanged();

private:
    /// Продлевает доступ, если срок токена подходит к концу.
    /// Без этого клиент однажды перестал бы работать целиком, без объяснений.
    void refreshTokenIfNeeded();
    /// Рассылает свежий токен рабочим потокам.
    void distributeToken();

    void setBusy(bool busy);
    void setStatus(const QString& status);
    void setWaitingForCode(bool waiting);

    Credentials m_creds;
    AuthManager* m_auth = nullptr;

    QThread m_syncThread;
    SyncWorker* m_sync = nullptr;
    /// Опрос новинок: у API Диска нет уведомлений об изменениях,
    /// поэтому единственный способ узнать о них — спрашивать самим.
    QTimer* m_pollTimer = nullptr;
    /// Повторные попытки выгрузки — страховка от сорвавшейся отправки.
    QTimer* m_retryTimer = nullptr;
    QTimer* m_tokenTimer = nullptr;
    QThread m_mountThread;
    MountWorker* m_mount = nullptr;
    QThread m_opsThread;
    FileOpsWorker* m_ops = nullptr;
    TrashModel* m_trash = nullptr;

    QString m_status;
    QString m_authUrl;
    QString m_spaceText;
    qreal m_progress = -1;
    qint64 m_totalSpace = 0;
    qint64 m_usedSpace = 0;
    bool m_busy = false;
    bool m_mounted = false;
    bool m_waitingForCode = false;
    bool m_online = true;
};

} // namespace orbita
