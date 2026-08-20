#include "orbita/AuthManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>

namespace orbita {
namespace {

constexpr auto kAuthorizeUrl = "https://oauth.yandex.ru/authorize";
constexpr auto kTokenUrl = "https://oauth.yandex.ru/token";

/// base64url без выравнивания — как требует RFC 7636.
QString base64Url(const QByteArray& data)
{
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString randomToken(int bytes)
{
    QByteArray buf(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(buf.begin(), buf.end());
    return base64Url(buf);
}

QByteArray successPage()
{
    const QByteArray body = QStringLiteral(
        "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
        "<title>Orbita</title><style>"
        "body{font-family:system-ui,sans-serif;background:#1b1e20;color:#eff0f1;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
        "div{text-align:center}h1{font-weight:500;margin:0 0 .5rem}"
        "p{color:#9aa0a6;margin:0}</style></head><body><div>"
        "<h1>Orbita подключена</h1><p>Вкладку можно закрыть.</p>"
        "</div></body></html>").toUtf8();

    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
           "Content-Length: " + QByteArray::number(body.size())
        + "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace

AuthManager::AuthManager(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

QString AuthManager::redirectUri() const
{
    if (m_oob)
        return QStringLiteral("https://oauth.yandex.ru/verification_code");
    return QStringLiteral("http://localhost:%1/callback").arg(m_port);
}

void AuthManager::submitCode(const QString& code)
{
    if (code.isEmpty()) {
        Q_EMIT failed(tr("Пустой код подтверждения"));
        return;
    }
    exchangeCode(code);
}

void AuthManager::startLogin(const QString& clientId)
{
    if (clientId.isEmpty()) {
        Q_EMIT failed(tr("Не задан client_id приложения"));
        return;
    }

    m_clientId = clientId;
    m_verifier = randomToken(48);
    m_state = randomToken(16);

    stopServer();
    if (!m_oob) {
        m_server = new QTcpServer(this);
        if (!m_server->listen(QHostAddress::LocalHost, m_port)) {
            Q_EMIT failed(tr("Не удалось занять порт %1: %2")
                              .arg(m_port).arg(m_server->errorString()));
            stopServer();
            return;
        }
        connect(m_server, &QTcpServer::newConnection, this, &AuthManager::handleConnection);
    }

    const QByteArray challenge = QCryptographicHash::hash(m_verifier.toLatin1(),
                                                          QCryptographicHash::Sha256);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    query.addQueryItem(QStringLiteral("code_challenge"), base64Url(challenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_state);

    QUrl url(QString::fromLatin1(kAuthorizeUrl));
    url.setQuery(query);

    Q_EMIT authorizationUrlReady(url);

    // xdg-open вместо QDesktopServices: демону не нужен QGuiApplication,
    // а значит клиент запускается и там, где сессии ещё нет.
    QProcess::startDetached(QStringLiteral("xdg-open"), { url.toString(QUrl::FullyEncoded) });
}

void AuthManager::handleConnection()
{
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket)
        return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        const QByteArray request = socket->readAll();
        const int lineEnd = request.indexOf("\r\n");
        if (lineEnd < 0)
            return; // заголовок ещё не пришёл целиком

        // "GET /callback?code=...&state=... HTTP/1.1"
        const QList<QByteArray> parts = request.left(lineEnd).split(' ');
        if (parts.size() < 2) {
            Q_EMIT failed(tr("Некорректный ответ браузера"));
            socket->disconnectFromHost();
            return;
        }

        const QUrlQuery query(QUrl(QString::fromUtf8(parts.at(1))).query());
        socket->write(successPage());
        socket->disconnectFromHost();

        const QString error = query.queryItemValue(QStringLiteral("error"));
        if (!error.isEmpty()) {
            stopServer();
            Q_EMIT failed(tr("Яндекс отказал в доступе: %1").arg(error));
            return;
        }

        // Сверка state — защита от подсунутого чужого кода.
        if (query.queryItemValue(QStringLiteral("state")) != m_state) {
            stopServer();
            Q_EMIT failed(tr("Не совпал параметр state"));
            return;
        }

        const QString code = query.queryItemValue(QStringLiteral("code"));
        if (code.isEmpty()) {
            stopServer();
            Q_EMIT failed(tr("Браузер не вернул код подтверждения"));
            return;
        }

        stopServer();
        exchangeCode(code);
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void AuthManager::exchangeCode(const QString& code)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    postToken(form);
}

void AuthManager::refresh(const QString& clientId, const QString& refreshToken)
{
    m_clientId = clientId;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    postToken(form);
}

void AuthManager::postToken(QUrlQuery form)
{
    QNetworkRequest request{ QUrl(QString::fromLatin1(kTokenUrl)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    // Обмен кода на токен проходит по одному PKCE.
    QNetworkReply* reply = m_net->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            const QString detail = obj.value(QStringLiteral("error_description")).toString();
            Q_EMIT failed(detail.isEmpty() ? reply->errorString() : detail);
            return;
        }

        Credentials creds;
        creds.accessToken = obj.value(QStringLiteral("access_token")).toString();
        creds.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
        const qint64 expiresIn = obj.value(QStringLiteral("expires_in")).toInteger();
        if (expiresIn > 0)
            creds.expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);

        if (!creds.isValid()) {
            Q_EMIT failed(tr("Ответ не содержит токена"));
            return;
        }
        Q_EMIT succeeded(creds);
    });
}

void AuthManager::stopServer()
{
    if (!m_server)
        return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

} // namespace orbita
