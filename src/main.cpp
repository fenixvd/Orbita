#include "orbita/AuthManager.h"
#include "orbita/CacheManager.h"
#include "orbita/DiskApi.h"
#include "orbita/MetadataStore.h"
#include "orbita/Notifier.h"
#include "orbita/Paths.h"
#include "orbita/RemoteOps.h"
#include "orbita/TokenStore.h"

#ifdef ORBITA_WITH_FUSE
#include "orbita/FuseMount.h"
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QQueue>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

using namespace orbita;

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

QString dbPath()
{
    return paths::databaseFile();
}

/// Действующий токен: при необходимости молча обновляется по refresh_token.
Credentials ensureToken()
{
    Credentials creds = TokenStore::load();
    if (!creds.isValid() || !creds.isExpired())
        return creds;

    if (creds.refreshToken.isEmpty())
        return {};

    AuthManager auth;
    Credentials refreshed;
    QEventLoop loop;
    QObject::connect(&auth, &AuthManager::succeeded, [&](const Credentials& c) {
        refreshed = c;
        loop.quit();
    });
    QObject::connect(&auth, &AuthManager::failed, [&](const QString& e) {
        err << "Не удалось обновить токен: " << e << Qt::endl;
        loop.quit();
    });
    auth.refresh(TokenStore::clientId(), creds.refreshToken);
    loop.exec();

    if (refreshed.isValid()) {
        // refresh_token Яндекс возвращает не всегда — старый остаётся рабочим.
        if (refreshed.refreshToken.isEmpty())
            refreshed.refreshToken = creds.refreshToken;
        TokenStore::save(refreshed);
    }
    return refreshed;
}

int cmdLogin(bool oob)
{
    const QString clientId = TokenStore::clientId();
    if (clientId.isEmpty()) {
        err << "Не задан client_id.\n"
               "Зарегистрируйте приложение на https://oauth.yandex.ru/client/new\n"
               "с правами на Яндекс.Диск и Redirect URI http://localhost:8123/callback,\n"
               "затем положите его в " << TokenStore::configDir() << "/config.json:\n"
               "  { \"client_id\": \"...\" }\n";
        return 1;
    }

    AuthManager auth;
    auth.setOobMode(oob);
    int result = 1;
    QEventLoop loop;

    QObject::connect(&auth, &AuthManager::authorizationUrlReady, [](const QUrl& url) {
        out << "Открываю браузер. Если не открылся — перейдите вручную:\n"
            << url.toString() << Qt::endl;
    });
    QObject::connect(&auth, &AuthManager::succeeded, [&](const Credentials& creds) {
        if (TokenStore::save(creds)) {
            out << "Готово. Токен сохранён в " << TokenStore::credentialsPath() << Qt::endl;
            result = 0;
        } else {
            err << "Токен получен, но записать его не удалось." << Qt::endl;
        }
        loop.quit();
    });
    QObject::connect(&auth, &AuthManager::failed, [&](const QString& e) {
        err << "Вход не удался: " << e << Qt::endl;
        loop.quit();
    });

    // Ждать вечно нельзя: если пользователь закрыл вкладку, процесс должен завершиться.
    QTimer::singleShot(std::chrono::minutes(5), &loop, [&] {
        err << "Истекло время ожидания входа." << Qt::endl;
        loop.quit();
    });

    auth.startLogin(clientId);

    if (oob) {
        out << "Вставьте код подтверждения со страницы Яндекса: " << Qt::flush;
        QTextStream in(stdin);
        auth.submitCode(in.readLine().trimmed());
    }

    loop.exec();
    return result;
}

int cmdStatus()
{
    const Credentials creds = TokenStore::load();
    out << "Настройки:  " << TokenStore::configDir() << "\n"
        << "База:       " << dbPath() << "\n"
        << "client_id:  " << (TokenStore::clientId().isEmpty() ? QStringLiteral("не задан")
                                                               : QStringLiteral("задан")) << "\n"
        << "Токен:      "
        << (!creds.isValid() ? QStringLiteral("нет")
                             : creds.isExpired() ? QStringLiteral("просрочен")
                                                 : QStringLiteral("действителен"))
        << Qt::endl;
    return 0;
}

int cmdList(const QString& path)
{
    const Credentials creds = ensureToken();
    if (!creds.isValid()) {
        err << "Нет токена — выполните: orbita login" << Qt::endl;
        return 1;
    }

    MetadataStore store;
    if (!store.open(dbPath())) {
        err << "Не удалось открыть базу: " << store.lastError() << Qt::endl;
        return 1;
    }

    DiskApi api;
    api.setToken(creds.accessToken);

    int result = 1;
    QEventLoop loop;
    QObject::connect(&api, &DiskApi::listed,
                     [&](const QString& dir, const QVector<Resource>& items, bool hasMore) {
                         if (!store.upsertBatch(items))
                             err << "Предупреждение: " << store.lastError() << Qt::endl;

                         for (const Resource& r : items) {
                             out << (r.isDir ? QStringLiteral("[папка] ") : QStringLiteral("        "))
                                 << r.name;
                             if (!r.isDir)
                                 out << "  " << r.size << " Б";
                             out << Qt::endl;
                         }
                         out << "-- " << items.size() << " элементов в " << dir
                             << (hasMore ? " (есть ещё страницы)" : "") << Qt::endl;
                         result = 0;
                         loop.quit();
                     });
    QObject::connect(&api, &DiskApi::errorOccurred, [&](const QString& e) {
        err << "Ошибка API: " << e << Qt::endl;
        loop.quit();
    });

    api.list(path);
    loop.exec();
    return result;
}

/// Одна страница листинга, синхронно. Рекурсивный обход без этого превращается
/// в лапшу из вложенных колбэков.
struct PageResult {
    QVector<Resource> items;
    bool hasMore = false;
    bool ok = false;
};

PageResult listBlocking(DiskApi& api, const QString& path, int offset)
{
    PageResult result;
    QEventLoop loop;
    QMetaObject::Connection c1 = QObject::connect(
        &api, &DiskApi::listed,
        [&](const QString&, const QVector<Resource>& items, bool hasMore) {
            result.items = items;
            result.hasMore = hasMore;
            result.ok = true;
            loop.quit();
        });
    QMetaObject::Connection c2 = QObject::connect(&api, &DiskApi::errorOccurred,
                                                  [&](const QString& e) {
                                                      err << "Ошибка API: " << e << Qt::endl;
                                                      loop.quit();
                                                  });
    api.list(path, 200, offset);
    loop.exec();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    return result;
}

/// Обходит Диск и складывает всё дерево в базу. Содержимое не качается —
/// именно поэтому даже терабайтный Диск «скачивается» за минуты и занимает мегабайты.
int cmdSync(const QString& root)
{
    const Credentials creds = ensureToken();
    if (!creds.isValid()) {
        err << "Нет токена — выполните: orbita login" << Qt::endl;
        return 1;
    }

    MetadataStore store;
    if (!store.open(dbPath())) {
        err << "Не удалось открыть базу: " << store.lastError() << Qt::endl;
        return 1;
    }

    DiskApi api;
    api.setToken(creds.accessToken);

    QQueue<QString> pending;
    pending.enqueue(root);
    int dirs = 0;
    int files = 0;

    while (!pending.isEmpty()) {
        const QString dir = pending.dequeue();
        int offset = 0;
        while (true) {
            const PageResult page = listBlocking(api, dir, offset);
            if (!page.ok)
                return 1;
            if (!store.upsertBatch(page.items))
                err << "Предупреждение: " << store.lastError() << Qt::endl;

            for (const Resource& r : page.items) {
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
        out << "\rПапок: " << dirs << "  файлов: " << files << "   " << Qt::flush;
    }

    out << "\nГотово. В базе " << dirs << " папок и " << files << " файлов." << Qt::endl;
    return 0;
}

#ifdef ORBITA_WITH_FUSE
/// Поддельные операции над «Диском»: подменяют только сеть, чтобы проверить
/// запись через настоящий кэш и настоящие вызовы FUSE.
class DemoOps : public RemoteOps {
public:
    bool upload(const QString& remotePath, const QString& localFile) override
    {
        out << "  [демо] выгрузка " << remotePath << " (" << QFileInfo(localFile).size()
            << " Б)" << Qt::endl;
        return true;
    }
    bool makeDir(const QString& remotePath) override
    {
        out << "  [демо] создана папка " << remotePath << Qt::endl;
        return true;
    }
    bool removeToTrash(const QString& remotePath) override
    {
        out << "  [демо] в корзину " << remotePath << Qt::endl;
        return true;
    }
    bool move(const QString& from, const QString& to) override
    {
        out << "  [демо] перемещение " << from << " -> " << to << Qt::endl;
        return true;
    }
    QString remoteMd5(const QString&) override
    {
        return {}; // в демо-режиме конфликтов не бывает
    }
    bool capacity(qint64* total, qint64* used) override
    {
        if (total)
            *total = 100LL * 1024 * 1024 * 1024;
        if (used)
            *used = 10LL * 1024 * 1024 * 1024;
        return true;
    }
};

/// Монтирует выдуманное дерево — проверка FUSE-слоя целиком, без сети.
int cmdMountDemo(const QString& mountPoint)
{
    const QString demoDb = QDir::tempPath() + QStringLiteral("/orbita-demo.db");
    QFile::remove(demoDb);

    MetadataStore store;
    if (!store.open(demoDb)) {
        err << "Не удалось открыть базу: " << store.lastError() << Qt::endl;
        return 1;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVector<Resource> demo;
    auto add = [&](const QString& path, bool isDir, qint64 size) {
        Resource r;
        r.path = path;
        r.name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
        r.isDir = isDir;
        r.size = size;
        r.modified = now;
        demo.append(r);
    };
    add(QStringLiteral("/Фото"), true, 0);
    add(QStringLiteral("/Документы"), true, 0);
    add(QStringLiteral("/Фото/кот.jpg"), false, 2 * 1024 * 1024);
    add(QStringLiteral("/Документы/договор.pdf"), false, 512 * 1024);
    add(QStringLiteral("/заметка.txt"), false, 4096);
    if (!store.upsertBatch(demo)) {
        err << store.lastError() << Qt::endl;
        return 1;
    }

    // Кэш настоящий — подменяется только загрузка из сети.
    const QString demoCache = QDir::tempPath() + QStringLiteral("/orbita-demo-content");
    QDir(demoCache).removeRecursively();
    CacheManager cache(demoCache, &store);
    cache.setFetcher([](const QString& remote, const QString& dest,
                        const ContentProvider::ProgressFn&) {
        QFile f(dest);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(QStringLiteral("Файл %1 материализован по требованию.\n").arg(remote).toUtf8());
        return true;
    });

    DemoOps ops;
    QDir().mkpath(mountPoint);
    out << "Демо-дерево смонтировано в " << mountPoint << ". Ctrl+C — размонтировать."
        << Qt::endl;

    FuseMount mount(store, cache, &ops);
    if (!mount.run(mountPoint, false)) {
        err << mount.lastError() << Qt::endl;
        return 1;
    }
    return 0;
}

int cmdMount(const QString& mountPoint, bool debug)
{
    const Credentials creds = ensureToken();
    if (!creds.isValid()) {
        err << "Нет токена — выполните: orbita login" << Qt::endl;
        return 1;
    }

    MetadataStore store;
    if (!store.open(dbPath())) {
        err << "Не удалось открыть базу: " << store.lastError() << Qt::endl;
        return 1;
    }

    CacheManager cache(paths::cacheDir(), &store);
    cache.setBudget(paths::cacheBudget());

    const QString token = creds.accessToken;
    cache.setFetcher([token](const QString& remote, const QString& dest,
                             const ContentProvider::ProgressFn& progress) {
        return DiskApi::downloadBlocking(token, remote, dest, progress);
    });
    cache.setRangeFetcher([token](const QString& remote, qint64 offset, qint64 size, char* buffer) {
        return DiskApi::readRangeBlocking(token, remote, offset, size, buffer);
    });

    DiskOps ops(token);

    QDir().mkpath(mountPoint);
    out << "Монтирую в " << mountPoint << ". Ctrl+C — размонтировать." << Qt::endl;

    FuseMount mount(store, cache, &ops);
    if (!mount.run(mountPoint, debug)) {
        err << mount.lastError() << Qt::endl;
        return 1;
    }
    return 0;
}
#endif

/// Переводит путь файловой системы в путь на Диске.
/// Пустая строка означает, что файл лежит вне смонтированной папки.
QString toDiskPath(const QString& localPath)
{
    const QString absolute = QFileInfo(localPath).absoluteFilePath();
    const QString mount = paths::mountPoint();
    if (!absolute.startsWith(mount))
        return {};

    const QString relative = absolute.mid(mount.size());
    return relative.isEmpty() ? QStringLiteral("/") : relative;
}

/// Показывает уведомление рабочего стола.
/// Команды из меню Dolphin запускаются без терминала, и сообщить о результате
/// иначе просто негде.
void notify(const QString& title, const QString& body = {}, bool problem = false)
{
    out << title << (body.isEmpty() ? QString() : QStringLiteral(": ") + body) << Qt::endl;
    Notifier::show(title, body, problem ? Notifier::Kind::Warning : Notifier::Kind::Info);
}

/// Отправляет файл или папку на Диск, сохраняя вложенность.
bool uploadRecursive(const QString& token, const QString& localPath, const QString& destDir,
                     int* sent)
{
    const QFileInfo info(localPath);
    const QString target = destDir + QLatin1Char('/') + info.fileName();

    if (!info.isDir()) {
        if (!DiskApi::uploadBlocking(token, target, info.absoluteFilePath()))
            return false;
        ++(*sent);
        return true;
    }

    DiskApi::mkdirBlocking(token, target); // могла уже существовать
    const QFileInfoList children = QDir(localPath).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& child : children) {
        if (!uploadRecursive(token, child.absoluteFilePath(), target, sent))
            return false;
    }
    return true;
}

/// Загрузка файлов с компьютера на Диск — пункт меню для всего, что вне Диска.
int cmdUpload(const QStringList& targets)
{
    const Credentials creds = ensureToken();
    if (!creds.isValid()) {
        notify(QObject::tr("Не выполнен вход"),
               QObject::tr("Откройте Orbita и войдите в аккаунт"), true);
        return 1;
    }

    const QString destDir = paths::uploadFolder();
    // Папка могла быть удалена — создаём молча, ошибку «уже есть» игнорируем.
    DiskApi::mkdirBlocking(creds.accessToken, destDir);

    MetadataStore store;
    const bool haveStore = store.open(dbPath());

    int sent = 0;
    int failed = 0;
    for (const QString& target : targets) {
        const QFileInfo info(target);
        if (!info.exists()) {
            ++failed;
            continue;
        }
        if (!uploadRecursive(creds.accessToken, info.absoluteFilePath(), destDir, &sent)) {
            // Кладём в очередь: она разберёт это при следующем запуске окна.
            if (haveStore) {
                store.addPendingUpload(info.absoluteFilePath(),
                                       destDir + QLatin1Char('/') + info.fileName());
            }
            ++failed;
        }
    }

    if (failed == 0)
        notify(QObject::tr("Загружено на Яндекс.Диск"),
               QObject::tr("Файлов: %1 → %2").arg(sent).arg(destDir));
    else
        notify(QObject::tr("Загружено не всё"),
               QObject::tr("Отправлено %1, отложено до следующей попытки: %2")
                   .arg(sent).arg(failed), true);
    return failed == 0 ? 0 : 1;
}

/// Действия над файлом для меню файлового менеджера.
int cmdFileAction(const QString& command, const QStringList& targets)
{
    const Credentials creds = ensureToken();
    if (!creds.isValid()) {
        notify(QObject::tr("Не выполнен вход"),
               QObject::tr("Откройте Orbita и войдите в аккаунт"), true);
        return 1;
    }

    MetadataStore store;
    if (!store.open(dbPath())) {
        notify(QObject::tr("Не удалось открыть базу"), store.lastError(), true);
        return 1;
    }

    CacheManager cache(paths::cacheDir(), &store);
    const QString token = creds.accessToken;
    cache.setFetcher([token](const QString& remote, const QString& dest,
                             const ContentProvider::ProgressFn& progress) {
        return DiskApi::downloadBlocking(token, remote, dest, progress);
    });

    int failures = 0;
    for (const QString& target : targets) {
        const QString diskPath = toDiskPath(target);
        if (diskPath.isEmpty()) {
            notify(QObject::tr("Файл не на Яндекс.Диске"),
                   QFileInfo(target).fileName(), true);
            ++failures;
            continue;
        }

        const QString name = QFileInfo(target).fileName();
        if (command == QLatin1String("pin")) {
            if (cache.pin(diskPath))
                notify(QObject::tr("Сохранено на устройстве"), name);
            else
                ++failures;
        } else if (command == QLatin1String("unpin")) {
            if (cache.evict(diskPath))
                notify(QObject::tr("Убрано с устройства"), name);
            else
                ++failures;
        } else if (command == QLatin1String("share")) {
            const QString url = DiskApi::publishBlocking(token, diskPath);
            if (url.isEmpty()) {
                ++failures;
            } else {
                store.setPublicUrl(diskPath, url);
                // Ссылку сразу в буфер обмена: за публикацией всегда следует
                // «отправить кому-то».
                if (!QStandardPaths::findExecutable(QStringLiteral("xclip")).isEmpty()) {
                    QProcess clip;
                    clip.start(QStringLiteral("xclip"),
                               { QStringLiteral("-selection"), QStringLiteral("clipboard") });
                    clip.write(url.toUtf8());
                    clip.closeWriteChannel();
                    clip.waitForFinished(2000);
                }
                notify(QObject::tr("Ссылка скопирована"), url);
            }
        } else if (command == QLatin1String("unshare")) {
            if (DiskApi::unpublishBlocking(token, diskPath)) {
                store.setPublicUrl(diskPath, QString());
                notify(QObject::tr("Доступ по ссылке закрыт"), name);
            } else {
                ++failures;
            }
        }
    }

    if (failures > 0)
        notify(QObject::tr("Не удалось обработать"),
               QObject::tr("Файлов: %1").arg(failures), true);
    return failures == 0 ? 0 : 1;
}

/// Самопроверка: отвечает на вопрос «почему не работает» без переписки.
int cmdDoctor()
{
    int problems = 0;
    auto check = [&](bool ok, const QString& what, const QString& hint) {
        out << (ok ? QStringLiteral("  [ ок ] ") : QStringLiteral("  [ нет ] ")) << what << Qt::endl;
        if (!ok) {
            ++problems;
            if (!hint.isEmpty())
                out << "         " << hint << Qt::endl;
        }
    };

    out << "Orbita " << ORBITA_VERSION << Qt::endl << Qt::endl;

    const Credentials creds = TokenStore::load();
    check(!TokenStore::clientId().isEmpty(), QObject::tr("client_id задан"), {});
    check(creds.isValid(), QObject::tr("Токен получен"),
          QObject::tr("orbita login"));
    check(creds.isValid() && !creds.isExpired(), QObject::tr("Токен действителен"),
          QObject::tr("Продлится сам при запуске окна"));

    MetadataStore store;
    const bool dbOk = store.open(dbPath());
    check(dbOk, QObject::tr("База метаданных открывается"), store.lastError());
    if (dbOk) {
        const int files = store.folderStats(QStringLiteral("/")).files;
        check(files > 0, QObject::tr("В базе есть дерево Диска (%1 файлов)").arg(files),
              QObject::tr("orbita sync"));

        const auto pending = store.pendingUploads();
        check(pending.isEmpty(), QObject::tr("Очередь отправки пуста"),
              QObject::tr("Ждут отправки: %1 — окно дошлёт их само").arg(pending.size()));
    }

    const QString mount = paths::mountPoint();
    QFile mounts(QStringLiteral("/proc/self/mountinfo"));
    bool mounted = false;
    if (mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        mounted = QString::fromUtf8(mounts.readAll()).contains(mount);
    check(mounted, QObject::tr("Диск подключён (%1)").arg(mount),
          QObject::tr("orbita mount %1").arg(mount));

    // Плагины Dolphin ставятся пакетом; при сборке вручную о них забывают.
    const QStringList pluginDirs = {
        QStringLiteral("/usr/lib64/qt6/plugins/kf6"),
        QStringLiteral("/usr/lib/qt6/plugins/kf6"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6"),
    };
    bool overlay = false;
    bool actions = false;
    for (const QString& dir : pluginDirs) {
        overlay = overlay || QFile::exists(dir + QStringLiteral("/overlayicon/liborbita-overlay.so"));
        actions = actions
            || QFile::exists(dir + QStringLiteral("/kfileitemaction/liborbita-fileitemaction.so"));
    }
    check(overlay, QObject::tr("Плагин эмблем для Dolphin установлен"),
          QObject::tr("sudo cmake --install build"));
    check(actions, QObject::tr("Плагин меню для Dolphin установлен"),
          QObject::tr("sudo cmake --install build"));

    out << Qt::endl
        << (problems == 0 ? QObject::tr("Всё в порядке.")
                          : QObject::tr("Замечаний: %1").arg(problems))
        << Qt::endl;
    return problems == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Orbita"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ORBITA_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Orbita — клиент Яндекс.Диска с файлами по требованию"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("команда"),
        QStringLiteral("login | logout | status | doctor | ls | sync | mount | "
                       "upload | pin | unpin | share | unshare"));
    QCommandLineOption loopbackOption(
        QStringLiteral("loopback"),
        QStringLiteral("Ловить ответ на localhost — только для приложений типа «веб-сервис»."));
    parser.addOption(loopbackOption);
    QCommandLineOption debugOption(QStringLiteral("debug"),
                                   QStringLiteral("Подробный вывод FUSE при монтировании."));
    parser.addOption(debugOption);
    QCommandLineOption demoOption(
        QStringLiteral("demo"),
        QStringLiteral("Смонтировать выдуманное дерево — проверка без учётной записи."));
    parser.addOption(demoOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0, QStringLiteral("status"));

    if (command == QLatin1String("login"))
        return cmdLogin(!parser.isSet(loopbackOption));
    if (command == QLatin1String("logout")) {
        TokenStore::clear();
        out << "Токен удалён." << Qt::endl;
        return 0;
    }
    if (command == QLatin1String("status"))
        return cmdStatus();
    if (command == QLatin1String("doctor"))
        return cmdDoctor();
    if (command == QLatin1String("ls"))
        return cmdList(args.value(1, QStringLiteral("/")));
    if (command == QLatin1String("sync"))
        return cmdSync(args.value(1, QStringLiteral("/")));

    // Действия для меню файлового менеджера: путь приходит обычный,
    // из смонтированной папки.
    if (command == QLatin1String("pin") || command == QLatin1String("unpin")
        || command == QLatin1String("share") || command == QLatin1String("unshare")
        || command == QLatin1String("upload")) {
        const QStringList targets = args.mid(1);
        if (targets.isEmpty()) {
            err << "Укажите файл" << Qt::endl;
            return 1;
        }
        return command == QLatin1String("upload") ? cmdUpload(targets)
                                                  : cmdFileAction(command, targets);
    }
    if (command == QLatin1String("mount")) {
#ifdef ORBITA_WITH_FUSE
        if (args.size() < 2) {
            err << "Укажите точку монтирования: orbita mount ~/Яндекс.Диск" << Qt::endl;
            return 1;
        }
        if (parser.isSet(demoOption))
            return cmdMountDemo(args.at(1));
        return cmdMount(args.at(1), parser.isSet(debugOption));
#else
        err << "Сборка без поддержки FUSE: установите fuse3-devel и пересоберите." << Qt::endl;
        return 1;
#endif
    }

    err << "Неизвестная команда: " << command << Qt::endl;
    parser.showHelp(1);
}
