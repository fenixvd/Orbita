#include "AppController.h"

#include "FileOpsWorker.h"
#include "TrashModel.h"
#include "Workers.h"
#include "orbita/AuthManager.h"
#include "orbita/Notifier.h"
#include "orbita/Paths.h"

#include <QClipboard>
#include <QNetworkInformation>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>

namespace orbita {

AppController::AppController(QObject* parent)
    : QObject(parent)
    , m_auth(new AuthManager(this))
    , m_sync(new SyncWorker)
    , m_mount(new MountWorker)
    , m_ops(new FileOpsWorker)
    , m_trash(new TrashModel(this))
{
    m_creds = TokenStore::load();
    m_status = loggedIn() ? tr("Готово") : tr("Требуется вход");

    connect(m_auth, &AuthManager::authorizationUrlReady, this, [this](const QUrl& url) {
        m_authUrl = url.toString();
        Q_EMIT authUrlChanged();
        setWaitingForCode(m_auth->oobMode());
        setStatus(tr("Подтвердите доступ в браузере"));
    });
    connect(m_auth, &AuthManager::succeeded, this, [this](const Credentials& creds) {
        // При продлении Яндекс не всегда возвращает refresh_token — старый
        // остаётся рабочим, и терять его нельзя.
        m_creds.accessToken = creds.accessToken;
        m_creds.expiresAt = creds.expiresAt;
        if (!creds.refreshToken.isEmpty())
            m_creds.refreshToken = creds.refreshToken;
        TokenStore::save(m_creds);
        setWaitingForCode(false);
        setBusy(false);
        setStatus(tr("Вход выполнен"));
        Q_EMIT loggedInChanged();
        distributeToken();
        refreshCapacity();
    });
    connect(m_auth, &AuthManager::failed, this, [this](const QString& error) {
        setWaitingForCode(false);
        setBusy(false);
        setStatus(tr("Вход не удался"));
        Q_EMIT errorOccurred(error);
    });

    m_sync->moveToThread(&m_syncThread);
    connect(&m_syncThread, &QThread::finished, m_sync, &QObject::deleteLater);
    connect(m_sync, &SyncWorker::progress, this, [this](int dirs, int files) {
        setStatus(tr("Обход Диска: %1 папок, %2 файлов").arg(dirs).arg(files));
        Q_EMIT treeChanged();
    });
    connect(m_sync, &SyncWorker::finished, this, [this](bool ok, const QString& error) {
        setBusy(false);
        setStatus(ok ? tr("Дерево обновлено") : tr("Обход прерван"));
        if (!ok && !error.isEmpty())
            Q_EMIT errorOccurred(error);
        Q_EMIT treeChanged();
        refreshCapacity();
    });
    connect(m_sync, &SyncWorker::capacity, this, [this](qint64 total, qint64 used) {
        m_totalSpace = total;
        m_usedSpace = used;
        m_spaceText = tr("Занято %1 из %2")
                          .arg(QLocale().formattedDataSize(used),
                               QLocale().formattedDataSize(total));
        Q_EMIT spaceChanged();
    });
    connect(m_sync, &SyncWorker::treeChanged, this, [this] {
        setStatus(tr("Появились изменения на Диске"));
        Q_EMIT treeChanged();
    });
    m_syncThread.start();

    // Раз в минуту — один дешёвый запрос вместо полного обхода.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(paths::pollSeconds() * 1000);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        // Настройку могли изменить в окне — подхватываем без перезапуска.
        m_pollTimer->setInterval(paths::pollSeconds() * 1000);
        if (!loggedIn() || m_busy || !m_online)
            return;
        QMetaObject::invokeMethod(m_sync, "pollChanges", Qt::QueuedConnection,
                                  Q_ARG(QString, m_creds.accessToken));
    });
    m_pollTimer->start();

    // Дозагрузка недоотправленного: при запуске и дальше по кругу.
    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(std::chrono::minutes(2));
    connect(m_retryTimer, &QTimer::timeout, this, [this] {
        if (loggedIn() && !m_busy && m_online)
            QMetaObject::invokeMethod(m_ops, "retryPending", Qt::QueuedConnection);
    });
    m_retryTimer->start();
    if (loggedIn())
        QMetaObject::invokeMethod(m_ops, "retryPending", Qt::QueuedConnection);

    // Токен живёт около года, но однажды он всё же кончится. Проверяем раз
    // в час и при запуске: иначе в один день перестанет работать всё сразу.
    m_tokenTimer = new QTimer(this);
    m_tokenTimer->setInterval(std::chrono::hours(1));
    connect(m_tokenTimer, &QTimer::timeout, this, &AppController::refreshTokenIfNeeded);
    m_tokenTimer->start();
    refreshTokenIfNeeded();

    // Состояние сети. Без связи опрашивать Диск и дёргать очередь незачем —
    // это только копит ошибки и сажает батарею.
    if (QNetworkInformation::loadDefaultBackend()) {
        auto* info = QNetworkInformation::instance();
        const auto apply = [this](QNetworkInformation::Reachability reach) {
            const bool online = reach != QNetworkInformation::Reachability::Disconnected;
            if (m_online == online)
                return;
            m_online = online;
            Q_EMIT onlineChanged();
            setStatus(online ? tr("Связь восстановлена") : tr("Нет связи с Яндекс.Диском"));
            if (!online) {
                Notifier::show(tr("Нет связи"), tr("Изменения отправятся, когда сеть вернётся"),
                               Notifier::Kind::Warning, QStringLiteral("network"));
            }
        };
        apply(info->reachability());
        connect(info, &QNetworkInformation::reachabilityChanged, this, apply);
    }

    if (loggedIn())
        refreshCapacity();

    m_mount->moveToThread(&m_mountThread);
    connect(&m_mountThread, &QThread::finished, m_mount, &QObject::deleteLater);
    connect(m_mount, &MountWorker::mounted, this, [this] {
        m_mounted = true;
        Q_EMIT mountedChanged();
        setStatus(tr("Диск подключён"));
        Notifier::show(tr("Диск подключён"), mountPoint(), Notifier::Kind::Info,
                       QStringLiteral("mount"));
    });
    connect(m_mount, &MountWorker::stopped, this, [this](const QString& error) {
        m_mounted = false;
        Q_EMIT mountedChanged();
        setStatus(tr("Диск отключён"));
        if (error.isEmpty()) {
            Notifier::show(tr("Диск отключён"), mountPoint(), Notifier::Kind::Info,
                           QStringLiteral("mount"));
        } else {
            Q_EMIT errorOccurred(error);
            Notifier::show(tr("Диск отключился с ошибкой"), error, Notifier::Kind::Warning,
                           QStringLiteral("mount"));
        }
    });
    m_mountThread.start();

    m_ops->moveToThread(&m_opsThread);
    m_ops->setToken(m_creds.accessToken); // поток ещё не запущен — можно напрямую
    connect(&m_opsThread, &QThread::finished, m_ops, &QObject::deleteLater);
    connect(m_ops, &FileOpsWorker::progress, this, [this](const QString& caption, qreal fraction) {
        setStatus(caption);
        m_progress = fraction;
        Q_EMIT progressChanged();
    });
    connect(m_ops, &FileOpsWorker::done, this, [this](bool ok, const QString& message) {
        setBusy(false);
        setStatus(message);
        m_progress = -1;
        Q_EMIT progressChanged();
        if (!ok)
            Q_EMIT errorOccurred(message);

        // Окно может быть в лотке — тогда это единственный способ узнать исход.
        // Общий канал: сообщения заменяют друг друга, а не копятся стопкой.
        Notifier::show(ok ? tr("Orbita") : tr("Orbita: не получилось"), message,
                       ok ? Notifier::Kind::Info : Notifier::Kind::Warning,
                       QStringLiteral("operations"));
    });
    connect(m_ops, &FileOpsWorker::treeChanged, this, [this] {
        Q_EMIT treeChanged();
        refreshCapacity();
    });
    connect(m_ops, &FileOpsWorker::trashLoaded, this, [this](const QVector<Resource>& items) {
        m_trash->setItems(items);
        setBusy(false);
    });
    connect(m_ops, &FileOpsWorker::published, this, [this](const QString&, const QString& url) {
        // За публикацией всегда следует «отправить кому-то» — кладём в буфер.
        copyToClipboard(url);
    });
    m_opsThread.start();
}

AppController::~AppController()
{
    // Иначе точка останется висеть и следующий запуск упрётся в занятый каталог.
    if (m_mounted) {
        m_mount->requestStop();
        m_mountThread.wait(3000);
    }

    m_syncThread.quit();
    m_syncThread.wait(2000);
    m_mountThread.quit();
    m_mountThread.wait(2000);
    m_opsThread.quit();
    m_opsThread.wait(2000);
}

void AppController::refreshTokenIfNeeded()
{
    if (!m_creds.isValid() || !m_creds.isExpired() || m_creds.refreshToken.isEmpty())
        return;

    setStatus(tr("Продлеваю доступ"));
    m_auth->refresh(TokenStore::clientId(), m_creds.refreshToken);
}

void AppController::distributeToken()
{
    // Рабочие потоки живут со своей копией токена — после продления
    // им нужно раздать новый, иначе продолжат ходить со старым.
    QMetaObject::invokeMethod(m_ops, "setToken", Qt::QueuedConnection,
                              Q_ARG(QString, m_creds.accessToken));
    Q_EMIT loggedInChanged();
}

QString AppController::mountPoint() const
{
    return paths::mountPoint();
}

qreal AppController::spaceRatio() const
{
    if (m_totalSpace <= 0)
        return 0;
    return static_cast<qreal>(m_usedSpace) / static_cast<qreal>(m_totalSpace);
}

void AppController::refreshDir(const QString& path)
{
    if (!loggedIn() || path.isEmpty())
        return;
    // Тихо: пользователь просто перешёл в папку, мигать индикатором незачем.
    QMetaObject::invokeMethod(m_sync, "refreshDir", Qt::QueuedConnection,
                              Q_ARG(QString, m_creds.accessToken), Q_ARG(QString, path));
}

void AppController::refreshCapacity()
{
    if (!loggedIn())
        return;
    QMetaObject::invokeMethod(m_sync, "fetchCapacity", Qt::QueuedConnection,
                              Q_ARG(QString, m_creds.accessToken));
}

void AppController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void AppController::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}

void AppController::setWaitingForCode(bool waiting)
{
    if (m_waitingForCode == waiting)
        return;
    m_waitingForCode = waiting;
    Q_EMIT waitingForCodeChanged();
}

void AppController::startLogin()
{
    const QString clientId = TokenStore::clientId();
    if (clientId.isEmpty()) {
        Q_EMIT errorOccurred(tr("Не задан client_id приложения в %1/config.json")
                                 .arg(TokenStore::configDir()));
        return;
    }
    setBusy(true);
    setStatus(tr("Открываю браузер"));
    m_auth->startLogin(clientId);
}

void AppController::submitCode(const QString& code)
{
    setStatus(tr("Обмениваю код на токен"));
    setBusy(true);
    m_auth->submitCode(code.trimmed());
}

void AppController::logout()
{
    if (m_mounted)
        unmount();
    TokenStore::clear();
    m_creds = {};
    setStatus(tr("Требуется вход"));
    Q_EMIT loggedInChanged();
}

void AppController::startSync()
{
    if (!loggedIn() || m_busy)
        return;
    setBusy(true);
    setStatus(tr("Обход Диска"));
    QMetaObject::invokeMethod(m_sync, "run", Qt::QueuedConnection,
                              Q_ARG(QString, m_creds.accessToken),
                              Q_ARG(QString, QStringLiteral("/")));
}

void AppController::mount()
{
    if (!loggedIn() || m_mounted)
        return;
    setStatus(tr("Подключаю Диск"));
    QMetaObject::invokeMethod(m_mount, "run", Qt::QueuedConnection,
                              Q_ARG(QString, m_creds.accessToken),
                              Q_ARG(QString, mountPoint()));
}

void AppController::unmount()
{
    if (!m_mounted)
        return;
    setStatus(tr("Отключаю Диск"));
    // Просим ФС завершиться саму: fusermount3 бессилен, пока папку держит Dolphin.
    m_mount->requestStop();
}

void AppController::openMountPoint()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(mountPoint()));
}

void AppController::copyToClipboard(const QString& text)
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void AppController::openFile(const QString& diskPath)
{
    if (!m_mounted) {
        Q_EMIT errorOccurred(tr("Сначала подключите Диск"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(mountPoint() + diskPath));
}

void AppController::uploadUrls(const QList<QUrl>& urls, const QString& destDir)
{
    QStringList localPaths;
    for (const QUrl& url : urls) {
        if (url.isLocalFile())
            localPaths.append(url.toLocalFile());
    }
    if (localPaths.isEmpty())
        return;

    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "upload", Qt::QueuedConnection,
                              Q_ARG(QStringList, localPaths), Q_ARG(QString, destDir));
}

void AppController::uploadFromUrl(const QString& sourceUrl, const QString& destDir,
                                  const QString& name)
{
    if (sourceUrl.trimmed().isEmpty())
        return;
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "uploadFromUrl", Qt::QueuedConnection,
                              Q_ARG(QString, sourceUrl.trimmed()), Q_ARG(QString, destDir),
                              Q_ARG(QString, name));
}

void AppController::createFolder(const QString& parentDir, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString path = parentDir.endsWith(QLatin1Char('/')) ? parentDir + trimmed
                                                              : parentDir + QLatin1Char('/') + trimmed;
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "createFolder", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::renameItem(const QString& path, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return;

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString parent = slash <= 0 ? QString() : path.left(slash);
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "rename", Qt::QueuedConnection, Q_ARG(QString, path),
                              Q_ARG(QString, parent + QLatin1Char('/') + trimmed));
}

void AppController::moveItems(const QStringList& paths, const QString& destDir)
{
    if (paths.isEmpty())
        return;
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "moveTo", Qt::QueuedConnection, Q_ARG(QStringList, paths),
                              Q_ARG(QString, destDir));
}

void AppController::removeItem(const QString& path)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "removeToTrash", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::removeItems(const QStringList& paths)
{
    for (const QString& path : paths)
        removeItem(path);
}

void AppController::pinItems(const QStringList& paths)
{
    for (const QString& path : paths)
        pinItem(path);
}

void AppController::unpinItems(const QStringList& paths)
{
    for (const QString& path : paths)
        unpinItem(path);
}

void AppController::duplicateItem(const QString& path)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "duplicate", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::publishItem(const QString& path, const QString& password, int availableDays)
{
    setBusy(true);
    const qint64 seconds = availableDays > 0 ? qint64(availableDays) * 24 * 60 * 60 : 0;
    QMetaObject::invokeMethod(m_ops, "publish", Qt::QueuedConnection, Q_ARG(QString, path),
                              Q_ARG(QString, password), Q_ARG(qint64, seconds));
}

void AppController::unpublishItem(const QString& path)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "unpublish", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::pinItem(const QString& path)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "pin", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::unpinItem(const QString& path)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "unpin", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::loadTrash()
{
    setBusy(true);
    setStatus(tr("Читаю корзину"));
    QMetaObject::invokeMethod(m_ops, "loadTrash", Qt::QueuedConnection);
}

void AppController::restoreFromTrash(const QString& trashPath)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "restoreFromTrash", Qt::QueuedConnection,
                              Q_ARG(QString, trashPath));
}

void AppController::deleteFromTrash(const QString& trashPath)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "deleteFromTrash", Qt::QueuedConnection,
                              Q_ARG(QString, trashPath));
}

void AppController::emptyTrash()
{
    setBusy(true);
    QMetaObject::invokeMethod(m_ops, "emptyTrash", Qt::QueuedConnection);
}

} // namespace orbita
