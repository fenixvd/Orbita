#include "orbita/TokenStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>

namespace orbita {
namespace {
constexpr auto kConfigFile = "config.json";
}

QString TokenStore::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Orbita");
}

QString TokenStore::credentialsPath()
{
    return configDir() + QStringLiteral("/credentials.json");
}

Credentials TokenStore::load()
{
    QFile f(credentialsPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    Credentials c;
    c.accessToken = obj.value(QStringLiteral("access_token")).toString();
    c.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    const qint64 expires = obj.value(QStringLiteral("expires_at")).toInteger();
    if (expires > 0)
        c.expiresAt = QDateTime::fromSecsSinceEpoch(expires, QTimeZone::UTC);
    return c;
}

bool TokenStore::save(const Credentials& creds)
{
    QDir().mkpath(configDir());

    QJsonObject obj;
    obj.insert(QStringLiteral("access_token"), creds.accessToken);
    obj.insert(QStringLiteral("refresh_token"), creds.refreshToken);
    if (creds.expiresAt.isValid())
        obj.insert(QStringLiteral("expires_at"), creds.expiresAt.toSecsSinceEpoch());

    QSaveFile f(credentialsPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return false;

    // Права выставляем после commit: QSaveFile создаёт файл заново.
    return QFile::setPermissions(credentialsPath(),
                                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool TokenStore::clear()
{
    return QFile::remove(credentialsPath());
}

namespace {

QString configValue(const QString& key, const char* envName)
{
    const QByteArray fromEnv = qgetenv(envName);
    if (!fromEnv.isEmpty())
        return QString::fromUtf8(fromEnv);

    QFile f(TokenStore::configDir() + QLatin1Char('/') + QLatin1String(kConfigFile));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(key).toString();
}

} // namespace

QString TokenStore::clientId()
{
    const QString configured = configValue(QStringLiteral("client_id"), "ORBITA_CLIENT_ID");
    if (!configured.isEmpty())
        return configured;

    // Зашит, чтобы клиент работал сразу после установки. Своё приложение
    // задаётся в config.json.
    return QStringLiteral(ORBITA_CLIENT_ID);
}

} // namespace orbita
