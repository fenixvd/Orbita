#include "orbita/Notifier.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantMap>

namespace orbita {
namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";

/// Ранее показанные уведомления по каналам — чтобы заменять, а не плодить.
QHash<QString, uint>& channelIds()
{
    static QHash<QString, uint> ids;
    return ids;
}

QMutex& channelMutex()
{
    static QMutex mutex;
    return mutex;
}

} // namespace

void Notifier::show(const QString& title, const QString& body, Kind kind, const QString& channel)
{
    QDBusInterface notifications(QLatin1String(kService), QLatin1String(kPath),
                                 QLatin1String(kService), QDBusConnection::sessionBus());
    if (!notifications.isValid())
        return; // службы уведомлений нет — не беда, работаем молча

    uint replaces = 0;
    if (!channel.isEmpty()) {
        QMutexLocker lock(&channelMutex());
        replaces = channelIds().value(channel, 0);
    }

    QVariantMap hints;
    // Значок приложения и связь с ярлыком: по ним оболочка покажет
    // уведомление как принадлежащее Orbita, а не «неизвестной программе».
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("ru.rainedev.orbita"));
    hints.insert(QStringLiteral("urgency"), kind == Kind::Warning ? uint(2) : uint(1));

    const QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"), QStringLiteral("Orbita"), replaces,
        QStringLiteral("ru.rainedev.orbita"), title, body, QStringList {}, hints,
        kind == Kind::Warning ? 8000 : 5000);

    if (reply.isValid() && !channel.isEmpty()) {
        QMutexLocker lock(&channelMutex());
        channelIds().insert(channel, reply.value());
    }
}

} // namespace orbita
