#include "ThumbnailProvider.h"

#include "orbita/Paths.h"
#include "orbita/TokenStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace orbita {
namespace {

constexpr auto kApiBase = "https://cloud-api.yandex.net/v1/disk";
/// Сколько ждать эскиз, прежде чем показать обычный значок.
constexpr unsigned long kWaitMs = 20000;

/// Размер с запасом — одна миниатюра идёт и в список, и на увеличенную строку.
int previewBox(const QSize& requested)
{
    const int side = qMax(requested.width(), requested.height());
    if (side <= 64)
        return 64;
    if (side <= 128)
        return 128;
    if (side <= 256)
        return 256;
    return 512;
}

QNetworkRequest authorized(const QUrl& url, const QString& token)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("OAuth %1").arg(token).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

} // namespace

ThumbnailFetcher::ThumbnailFetcher(QObject* parent)
    : QObject(parent)
{
}

void ThumbnailFetcher::start(quint64 id, const QString& diskPath, int box, const QString& token)
{
    // Менеджер создаём в своём потоке при первом запросе — иначе он
    // принадлежал бы потоку, который нас создал.
    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    requestPreviewLink(id, diskPath, box, token);
}

void ThumbnailFetcher::requestPreviewLink(quint64 id, const QString& diskPath, int box,
                                          const QString& token)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), diskPath);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("preview"));
    query.addQueryItem(QStringLiteral("preview_size"), QStringLiteral("%1x").arg(box));
    query.addQueryItem(QStringLiteral("preview_crop"), QStringLiteral("false"));

    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/resources"));
    url.setQuery(query);

    QNetworkReply* reply = m_net->get(authorized(url, token));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, token] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT finished(id, QImage());
            return;
        }

        const QString previewUrl = QJsonDocument::fromJson(reply->readAll()).object()
                                       .value(QStringLiteral("preview")).toString();
        // Поле есть только у графических форматов; для прочих — обычный значок.
        if (previewUrl.isEmpty()) {
            Q_EMIT finished(id, QImage());
            return;
        }
        requestImageData(id, previewUrl, token);
    });
}

void ThumbnailFetcher::requestImageData(quint64 id, const QString& url, const QString& token)
{
    // Превью тоже требует токена: ссылка сама по себе доступа не даёт.
    QNetworkReply* reply = m_net->get(authorized(QUrl(url), token));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();

        QImage image;
        if (reply->error() == QNetworkReply::NoError)
            image.loadFromData(reply->readAll());
        Q_EMIT finished(id, image);
    });
}

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_fetcher(new ThumbnailFetcher)
{
    QDir().mkpath(paths::thumbsDir());

    m_fetcher->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_fetcher, &QObject::deleteLater);
    QObject::connect(m_fetcher, &ThumbnailFetcher::finished, m_fetcher,
                     [this](quint64 id, const QImage& image) {
                         // Поток загрузчика: кладём результат и будим ожидающего.
                         QMutexLocker lock(&m_mutex);
                         m_results.insert(id, image);
                         m_ready.wakeAll();
                     });
    m_thread.start();
}

ThumbnailProvider::~ThumbnailProvider()
{
    m_thread.quit();
    m_thread.wait(2000);
}

QString ThumbnailProvider::token()
{
    QMutexLocker lock(&m_mutex);
    if (m_token.isEmpty())
        m_token = TokenStore::load().accessToken;
    return m_token;
}

QString ThumbnailProvider::diskCachePath(const QString& diskPath, int box) const
{
    const QByteArray key = QCryptographicHash::hash(diskPath.toUtf8(), QCryptographicHash::Sha1);
    return paths::thumbsDir()
        + QStringLiteral("/%1-%2.png").arg(QString::fromLatin1(key.toHex())).arg(box);
}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString diskPath = id.startsWith(QLatin1Char('/')) ? id : QLatin1Char('/') + id;
    const int box = previewBox(requestedSize);

    // Список прокручивают туда-сюда — виденное не перезапрашиваем.
    const QString cached = diskCachePath(diskPath, box);
    QImage image;
    if (image.load(cached)) {
        if (size)
            *size = image.size();
        return image;
    }

    const QString accessToken = token();
    if (accessToken.isEmpty())
        return {};

    quint64 requestId = 0;
    {
        QMutexLocker lock(&m_mutex);
        requestId = m_nextId++;
    }

    QMetaObject::invokeMethod(m_fetcher, "start", Qt::QueuedConnection,
                              Q_ARG(quint64, requestId), Q_ARG(QString, diskPath),
                              Q_ARG(int, box), Q_ARG(QString, accessToken));

    QMutexLocker lock(&m_mutex);
    while (!m_results.contains(requestId)) {
        if (!m_ready.wait(&m_mutex, kWaitMs))
            return {}; // не дождались — пусть остаётся значок типа файла
    }
    image = m_results.take(requestId);
    lock.unlock();

    if (image.isNull())
        return {};

    image.save(cached, "PNG");
    if (size)
        *size = image.size();
    return image;
}

} // namespace orbita
