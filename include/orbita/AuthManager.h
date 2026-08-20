#pragma once

#include "orbita/TokenStore.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

class QTcpServer;
class QNetworkAccessManager;

namespace orbita {

/// Вход через Яндекс ID: Authorization Code + PKCE.
class AuthManager : public QObject {
    Q_OBJECT
public:
    explicit AuthManager(QObject* parent = nullptr);

    /// Порт должен совпадать с redirect_uri, указанным в настройках приложения
    /// на oauth.yandex.ru, — Яндекс сверяет его точно.
    void setCallbackPort(quint16 port) { m_port = port; }
    QString redirectUri() const;

    /// Режим без локального сервера: Яндекс показывает код, пользователь его вставляет.
    ///
    /// Включён по умолчанию: у приложений типа «для доступа к API» redirect_uri
    /// изменить нельзя, он всегда verification_code. Локальный перехват возможен
    /// только для приложений типа «веб-сервис».
    void setOobMode(bool enabled) { m_oob = enabled; }
    bool oobMode() const { return m_oob; }

    /// Поднимает локальный слушатель и открывает страницу согласия в браузере.
    void startLogin(const QString& clientId);

    /// Ввод кода вручную (режим --oob).
    void submitCode(const QString& code);

    /// Меняет refresh_token на свежий access_token.
    void refresh(const QString& clientId, const QString& refreshToken);

Q_SIGNALS:
    void succeeded(const orbita::Credentials& creds);
    void failed(const QString& error);
    /// Ссылка на случай, если браузер не открылся сам, — её можно показать пользователю.
    void authorizationUrlReady(const QUrl& url);

private:
    void handleConnection();
    void exchangeCode(const QString& code);
    void postToken(QUrlQuery query);
    void stopServer();

    QTcpServer* m_server = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    QString m_clientId;
    QString m_verifier;
    QString m_state;
    quint16 m_port = 8123;
    bool m_oob = true;
};

} // namespace orbita
