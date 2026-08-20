#pragma once

#include <QDateTime>
#include <QString>

namespace orbita {

/// Выданные Яндекс ID учётные данные.
struct Credentials {
    QString accessToken;
    QString refreshToken;
    QDateTime expiresAt;

    bool isValid() const { return !accessToken.isEmpty(); }
    /// Запас в минуту, чтобы не словить протухание прямо в середине запроса.
    bool isExpired() const
    {
        return expiresAt.isValid() && QDateTime::currentDateTimeUtc().addSecs(60) >= expiresAt;
    }
};

/// Хранит токен в ~/.config/Orbita/credentials.json с правами 0600.
///
/// Файл, а не KWallet: клиент должен уметь стартовать до входа в сессию
/// (автозапуск, systemd --user), когда кошелёк ещё заперт.
class TokenStore {
public:
    /// Каталог настроек: ~/.config/Orbita
    static QString configDir();
    static QString credentialsPath();

    static Credentials load();
    /// Пишет атомарно и сразу выставляет 0600 — токен это доступ ко всему Диску.
    static bool save(const Credentials& creds);
    static bool clear();

    /// client_id приложения: сначала $ORBITA_CLIENT_ID, затем config.json.
    /// Секретом не является — виден в адресной строке браузера при входе.
    static QString clientId();
};

} // namespace orbita
