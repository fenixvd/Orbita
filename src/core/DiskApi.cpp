#include "orbita/DiskApi.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUrlQuery>
#include <cstring>

namespace orbita {
namespace {

constexpr auto kApiBase = "https://cloud-api.yandex.net/v1/disk";

QNetworkRequest makeRequest(const QUrl& url, const QString& token)
{
    QNetworkRequest req(url);
    // У Яндекса своя схема: "OAuth <токен>", не "Bearer".
    req.setRawHeader("Authorization", QStringLiteral("OAuth %1").arg(token).toUtf8());
    // API отвечает только JSON и требует заявить это явно.
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

Resource resourceFromJson(const QJsonObject& obj)
{
    Resource r;
    r.path = DiskApi::normalizePath(obj.value(QStringLiteral("path")).toString());
    r.name = obj.value(QStringLiteral("name")).toString();
    r.isDir = obj.value(QStringLiteral("type")).toString() == QLatin1String("dir");
    r.size = obj.value(QStringLiteral("size")).toInteger();
    r.md5 = obj.value(QStringLiteral("md5")).toString();
    r.modified = QDateTime::fromString(obj.value(QStringLiteral("modified")).toString(),
                                       Qt::ISODate);
    r.publicUrl = obj.value(QStringLiteral("public_url")).toString();
    // Только для содержимого Корзины; у остальных поля нет, и подставлять
    // сюда корень было бы враньём.
    const QString origin = obj.value(QStringLiteral("origin_path")).toString();
    if (!origin.isEmpty())
        r.originPath = DiskApi::normalizePath(origin);
    return r;
}

QString errorFromReply(QNetworkReply* reply, const QByteArray& body)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString message = obj.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? reply->errorString() : message;
}

} // namespace

DiskApi::DiskApi(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

QString DiskApi::normalizePath(const QString& apiPath)
{
    QString path = apiPath;
    // API возвращает пути с префиксом пространства имён: "disk:/Фото" для
    // самого Диска и "trash:/Файл_хеш" для Корзины. Обратно их принимает
    // уже без префикса, поэтому снимаем оба.
    if (path.startsWith(QLatin1String("disk:")))
        path.remove(0, 5);
    else if (path.startsWith(QLatin1String("trash:")))
        path.remove(0, 6);
    if (path.isEmpty())
        path = QStringLiteral("/");
    return path;
}

void DiskApi::list(const QString& path, int limit, int offset)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), path);
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    // Только нужные поля — иначе ответ на большую папку весит мегабайты.
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("_embedded.total,_embedded.items.name,_embedded.items.path,"
                                      "_embedded.items.type,_embedded.items.size,"
                                      "_embedded.items.md5,_embedded.items.modified,"
                                      "_embedded.items.public_url"));

    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/resources"));
    url.setQuery(query);

    QNetworkReply* reply = m_net->get(makeRequest(url, m_token));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, limit, offset] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT errorOccurred(errorFromReply(reply, body));
            return;
        }

        const QJsonObject embedded = QJsonDocument::fromJson(body).object()
                                         .value(QStringLiteral("_embedded")).toObject();
        const QJsonArray items = embedded.value(QStringLiteral("items")).toArray();

        QVector<Resource> out;
        out.reserve(items.size());
        for (const QJsonValue& v : items)
            out.append(resourceFromJson(v.toObject()));

        const int total = embedded.value(QStringLiteral("total")).toInt();
        Q_EMIT listed(path, out, offset + items.size() < total && !items.isEmpty());
        Q_UNUSED(limit);
    });
}

namespace {

/// Ждёт ответа. Очередь событий своя на поток, так что годится и вне главного.
void waitFor(QNetworkReply* reply)
{
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Что имеет смысл повторить. Отказ в доступе повторять бессмысленно.
bool isTransient(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::InternalServerError:
    case QNetworkReply::ServiceUnavailableError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

constexpr int kMaxAttempts = 3;

int statusOf(QNetworkReply* reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QUrl apiUrl(const QString& endpoint, const QUrlQuery& query)
{
    QUrl url(QString::fromLatin1(kApiBase) + endpoint);
    url.setQuery(query);
    return url;
}

/// 202 — работа идёт на стороне Яндекса. Без ожидания отчитались бы раньше времени.
bool awaitOperation(QNetworkAccessManager& net, const QString& token, const QByteArray& body,
                    int maxAttempts = 100)
{
    const QString href = QJsonDocument::fromJson(body).object()
                             .value(QStringLiteral("href")).toString();
    if (href.isEmpty())
        return true; // операция без ссылки на статус — считаем выполненной

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        QNetworkReply* reply = net.get(makeRequest(QUrl(href), token));
        waitFor(reply);
        const QByteArray payload = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        if (failed)
            return false;

        const QString status = QJsonDocument::fromJson(payload).object()
                                   .value(QStringLiteral("status")).toString();
        if (status == QLatin1String("success"))
            return true;
        if (status == QLatin1String("failed"))
            return false;

        QThread::msleep(300);
    }
    return false;
}

/// Выполняет запрос, повторяя его при временных сбоях сети.
/// `send` вызывается заново на каждой попытке — QNetworkReply одноразовый.
QByteArray runWithRetry(const std::function<QNetworkReply*()>& send, bool* ok, int* status = nullptr)
{
    QByteArray body;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        QNetworkReply* reply = send();
        waitFor(reply);
        body = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        if (status)
            *status = statusOf(reply);
        reply->deleteLater();

        if (error == QNetworkReply::NoError) {
            *ok = true;
            return body;
        }
        if (!isTransient(error) || attempt == kMaxAttempts)
            break;

        QThread::msleep(300 * attempt); // разрастающаяся пауза
    }
    *ok = false;
    return body;
}

/// Успех для изменяющих операций: 2xx, при 202 — с ожиданием операции.
bool finishMutation(QNetworkAccessManager& net, const QString& token,
                    const std::function<QNetworkReply*()>& send,
                    int maxAttempts = 100)
{
    bool ok = false;
    int status = 0;
    const QByteArray body = runWithRetry(send, &ok, &status);
    if (!ok)
        return false;
    if (status == 202)
        return awaitOperation(net, token, body, maxAttempts);
    return status >= 200 && status < 300;
}

} // namespace

namespace {

/// Ссылка живёт ~30 минут, а чтений за это время бывают сотни.
struct HrefCache {
    QMutex mutex;
    QHash<QString, QPair<QString, QDateTime>> entries;
};

HrefCache& hrefCache()
{
    static HrefCache cache;
    return cache;
}

} // namespace

QString DiskApi::contentHref(const QString& token, const QString& remotePath)
{
    {
        QMutexLocker lock(&hrefCache().mutex);
        const auto it = hrefCache().entries.constFind(remotePath);
        if (it != hrefCache().entries.cend()
            && it->second > QDateTime::currentDateTimeUtc()) {
            return it->first;
        }
    }

    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] {
            return net.get(makeRequest(apiUrl(QStringLiteral("/resources/download"), query), token));
        },
        &ok);
    if (!ok)
        return {};

    const QString href = QJsonDocument::fromJson(body).object()
                             .value(QStringLiteral("href")).toString();
    if (href.isEmpty())
        return {};

    {
        // С запасом: протухшая ссылка посреди чтения хуже лишнего запроса.
        QMutexLocker lock(&hrefCache().mutex);
        hrefCache().entries.insert(remotePath,
                                   { href, QDateTime::currentDateTimeUtc().addSecs(20 * 60) });
    }
    return href;
}

qint64 DiskApi::readRangeBlocking(const QString& token, const QString& remotePath,
                                  qint64 offset, qint64 size, char* buffer)
{
    if (size <= 0)
        return 0;

    const QString href = contentHref(token, remotePath);
    if (href.isEmpty())
        return -1;

    QNetworkAccessManager net;
    QNetworkRequest request = makeRequest(QUrl(href), token);
    request.setRawHeader("Range",
                         QStringLiteral("bytes=%1-%2")
                             .arg(offset)
                             .arg(offset + size - 1)
                             .toLatin1());

    bool ok = false;
    int status = 0;
    const QByteArray body = runWithRetry([&] { return net.get(request); }, &ok, &status);
    if (!ok)
        return -1;

    // 200 вместо 206 означает, что диапазон не поддержан и пришёл весь файл:
    // тогда нужный кусок вырезаем сами.
    QByteArray chunk = body;
    if (status == 200 && body.size() > size)
        chunk = body.mid(offset, size);

    const qint64 count = qMin<qint64>(chunk.size(), size);
    std::memcpy(buffer, chunk.constData(), count);
    return count;
}

bool DiskApi::uploadBlocking(const QString& token,
                             const QString& remotePath,
                             const QString& localFile,
                             const std::function<void(qint64, qint64)>& onProgress)
{
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QNetworkAccessManager net;

    // Шаг 1: куда лить.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);
    query.addQueryItem(QStringLiteral("overwrite"), QStringLiteral("true"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] {
            return net.get(makeRequest(apiUrl(QStringLiteral("/resources/upload"), query), token));
        },
        &ok);
    if (!ok)
        return false;

    const QString href = QJsonDocument::fromJson(body).object()
                             .value(QStringLiteral("href")).toString();
    if (href.isEmpty())
        return false;

    // Шаг 2: сам файл. Ссылка сама по себе пропуск, токен не нужен.
    QNetworkRequest put(QUrl{ href });
    put.setHeader(QNetworkRequest::ContentLengthHeader, file.size());
    QNetworkReply* reply = net.put(put, &file);
    if (onProgress)
        QObject::connect(reply, &QNetworkReply::uploadProgress, onProgress);
    waitFor(reply);

    const int status = statusOf(reply);
    const bool uploaded = reply->error() == QNetworkReply::NoError
        && status >= 200 && status < 300;
    reply->deleteLater();
    return uploaded;
}

bool DiskApi::lastUploadedBlocking(const QString& token, QVector<Resource>* items, int limit)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("items.name,items.path,items.type,items.size,"
                                      "items.md5,items.modified,items.public_url"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] {
            return net.get(
                makeRequest(apiUrl(QStringLiteral("/resources/last-uploaded"), query), token));
        },
        &ok);
    if (!ok || !items)
        return false;

    const QJsonArray entries = QJsonDocument::fromJson(body).object()
                                   .value(QStringLiteral("items")).toArray();
    items->clear();
    items->reserve(entries.size());
    for (const QJsonValue& v : entries)
        items->append(resourceFromJson(v.toObject()));
    return true;
}

bool DiskApi::trashListBlocking(const QString& token, QVector<Resource>* items,
                                int limit, int offset)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), QStringLiteral("/"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    query.addQueryItem(QStringLiteral("sort"), QStringLiteral("-deleted"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] { return net.get(makeRequest(apiUrl(QStringLiteral("/trash/resources"), query), token)); },
        &ok);
    if (!ok || !items)
        return false;

    const QJsonArray entries = QJsonDocument::fromJson(body).object()
                                   .value(QStringLiteral("_embedded")).toObject()
                                   .value(QStringLiteral("items")).toArray();
    items->clear();
    items->reserve(entries.size());
    for (const QJsonValue& v : entries)
        items->append(resourceFromJson(v.toObject()));
    return true;
}

bool DiskApi::trashRestoreBlocking(const QString& token, const QString& trashPath)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), trashPath);
    query.addQueryItem(QStringLiteral("overwrite"), QStringLiteral("false"));

    return finishMutation(net, token, [&] {
        return net.put(makeRequest(apiUrl(QStringLiteral("/trash/resources/restore"), query), token),
                       QByteArray());
    });
}

bool DiskApi::trashDeleteBlocking(const QString& token, const QString& trashPath)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    // Без пути удаляется всё содержимое корзины.
    if (!trashPath.isEmpty())
        query.addQueryItem(QStringLiteral("path"), trashPath);

    return finishMutation(net, token, [&] {
        return net.deleteResource(
            makeRequest(apiUrl(QStringLiteral("/trash/resources"), query), token));
    });
}

bool DiskApi::statBlocking(const QString& token, const QString& remotePath, Resource* out)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("name,path,type,size,md5,modified,public_url"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] { return net.get(makeRequest(apiUrl(QStringLiteral("/resources"), query), token)); },
        &ok);
    if (!ok)
        return false;

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    if (!obj.contains(QStringLiteral("path")))
        return false;
    if (out)
        *out = resourceFromJson(obj);
    return true;
}

bool DiskApi::copyBlocking(const QString& token, const QString& from, const QString& to,
                           bool overwrite)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), from);
    query.addQueryItem(QStringLiteral("path"), to);
    query.addQueryItem(QStringLiteral("overwrite"),
                       overwrite ? QStringLiteral("true") : QStringLiteral("false"));

    // Копирование папки Яндекс делает асинхронно и может думать долго.
    return finishMutation(
        net, token,
        [&] {
            return net.post(makeRequest(apiUrl(QStringLiteral("/resources/copy"), query), token),
                            QByteArray());
        },
        2000);
}

QString DiskApi::publishBlocking(const QString& token, const QString& remotePath,
                                 const QString& password, qint64 availableUntil)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);

    // Пароль и срок жизни передаются JSON-телом, а не параметрами адреса,
    // и требуют современного формата доступа.
    QByteArray payload;
    if (!password.isEmpty() || availableUntil > 0) {
        query.addQueryItem(QStringLiteral("allow_address_access"), QStringLiteral("true"));

        QJsonObject publicSettings;
        if (availableUntil > 0)
            publicSettings.insert(QStringLiteral("available_until"), availableUntil);
        if (!password.isEmpty())
            publicSettings.insert(QStringLiteral("password"), password);

        QJsonArray accesses;
        QJsonObject everyone;
        everyone.insert(QStringLiteral("macros"), QJsonArray { QStringLiteral("all") });
        everyone.insert(QStringLiteral("rights"), QJsonArray { QStringLiteral("read") });
        accesses.append(everyone);
        publicSettings.insert(QStringLiteral("accesses"), accesses);

        QJsonObject root;
        root.insert(QStringLiteral("public_settings"), publicSettings);
        payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    if (!finishMutation(net, token, [&] {
            QNetworkRequest request = makeRequest(
                apiUrl(QStringLiteral("/resources/publish"), query), token);
            if (!payload.isEmpty()) {
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
            }
            return net.put(request, payload);
        })) {
        return {};
    }

    // Публикация возвращает лишь ссылку на ресурс — саму публичную ссылку
    // приходится забирать отдельным запросом метаданных.
    QUrlQuery metaQuery;
    metaQuery.addQueryItem(QStringLiteral("path"), remotePath);
    metaQuery.addQueryItem(QStringLiteral("fields"), QStringLiteral("public_url"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] { return net.get(makeRequest(apiUrl(QStringLiteral("/resources"), metaQuery), token)); },
        &ok);
    if (!ok)
        return {};

    return QJsonDocument::fromJson(body).object()
        .value(QStringLiteral("public_url")).toString();
}

bool DiskApi::unpublishBlocking(const QString& token, const QString& remotePath)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);

    return finishMutation(net, token, [&] {
        return net.put(makeRequest(apiUrl(QStringLiteral("/resources/unpublish"), query), token),
                       QByteArray());
    });
}

bool DiskApi::capacityBlocking(const QString& token, qint64* totalSpace, qint64* usedSpace,
                               qint64* trashSize)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("total_space,used_space,trash_size"));

    bool ok = false;
    const QByteArray body = runWithRetry(
        [&] { return net.get(makeRequest(apiUrl(QString(), query), token)); }, &ok);
    if (!ok)
        return false;

    const QJsonObject disk = QJsonDocument::fromJson(body).object();
    if (!disk.contains(QStringLiteral("total_space")))
        return false;

    if (totalSpace)
        *totalSpace = disk.value(QStringLiteral("total_space")).toInteger();
    if (usedSpace)
        *usedSpace = disk.value(QStringLiteral("used_space")).toInteger();
    if (trashSize)
        *trashSize = disk.value(QStringLiteral("trash_size")).toInteger();
    return true;
}

bool DiskApi::uploadFromUrlBlocking(const QString& token, const QString& remotePath,
                                    const QString& sourceUrl)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);
    query.addQueryItem(QStringLiteral("url"), sourceUrl);

    // Качает сам Яндекс, это минуты — ждём дольше обычного.
    constexpr int kPatientAttempts = 2000; // около десяти минут
    return finishMutation(
        net, token,
        [&] {
            return net.post(makeRequest(apiUrl(QStringLiteral("/resources/upload"), query), token),
                            QByteArray());
        },
        kPatientAttempts);
}

bool DiskApi::mkdirBlocking(const QString& token, const QString& remotePath)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);

    return finishMutation(net, token, [&] {
        return net.put(makeRequest(apiUrl(QStringLiteral("/resources"), query), token),
                       QByteArray());
    });
}

bool DiskApi::removeBlocking(const QString& token, const QString& remotePath, bool permanently)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);
    query.addQueryItem(QStringLiteral("permanently"),
                       permanently ? QStringLiteral("true") : QStringLiteral("false"));

    return finishMutation(net, token, [&] {
        return net.deleteResource(makeRequest(apiUrl(QStringLiteral("/resources"), query), token));
    });
}

bool DiskApi::moveBlocking(const QString& token, const QString& from, const QString& to,
                           bool overwrite)
{
    QNetworkAccessManager net;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), from);
    query.addQueryItem(QStringLiteral("path"), to);
    query.addQueryItem(QStringLiteral("overwrite"),
                       overwrite ? QStringLiteral("true") : QStringLiteral("false"));

    return finishMutation(net, token, [&] {
        return net.post(makeRequest(apiUrl(QStringLiteral("/resources/move"), query), token),
                        QByteArray());
    });
}

bool DiskApi::downloadBlocking(const QString& token,
                               const QString& remotePath,
                               const QString& destFile,
                               const std::function<void(qint64, qint64)>& onProgress)
{
    QNetworkAccessManager net;

    // Шаг 1: ссылка на содержимое. Она одноразовая и живёт недолго.
    QUrl hrefUrl(QString::fromLatin1(kApiBase) + QStringLiteral("/resources/download"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), remotePath);
    hrefUrl.setQuery(query);

    bool ok = false;
    const QByteArray meta = runWithRetry([&] { return net.get(makeRequest(hrefUrl, token)); }, &ok);
    if (!ok)
        return false;

    const QString href = QJsonDocument::fromJson(meta).object()
                             .value(QStringLiteral("href")).toString();
    if (href.isEmpty())
        return false;

    // Шаг 2: собственно файл. Пишем потоком, чтобы не держать его целиком в памяти.
    QFile out(destFile);
    if (!out.open(QIODevice::WriteOnly))
        return false;

    QNetworkReply* reply = net.get(makeRequest(QUrl(href), token));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] { out.write(reply->readAll()); });
    if (onProgress)
        QObject::connect(reply, &QNetworkReply::downloadProgress, onProgress);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    out.write(reply->readAll());
    out.close();

    const bool downloaded = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (!downloaded)
        QFile::remove(destFile);
    return downloaded;
}

} // namespace orbita
