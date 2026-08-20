#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QWindow>

#include <unistd.h>

namespace orbita {
namespace {

/// Имя привязано к пользователю: у каждого свой Диск и свой экземпляр.
QString socketName()
{
    return QStringLiteral("orbita-gui-%1").arg(::getuid());
}

} // namespace

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
{
}

bool SingleInstance::takeOwnership()
{
    // Сначала пробуем достучаться до работающей копии.
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (socket.waitForConnected(300)) {
        socket.write("show");
        socket.waitForBytesWritten(300);
        return false;
    }

    m_server = new QLocalServer(this);
    // Если прошлый запуск завершился неаккуратно, сокет остаётся в файловой
    // системе и мешает встать заново — снимаем его.
    QLocalServer::removeServer(socketName());
    if (!m_server->listen(socketName()))
        return true; // не смогли занять — работаем как есть, лучше так, чем не запуститься

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        QLocalSocket* client = m_server->nextPendingConnection();
        connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        showWindow();
    });
    return true;
}

void SingleInstance::showWindow()
{
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

} // namespace orbita
