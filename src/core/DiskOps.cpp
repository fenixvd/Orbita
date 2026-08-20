#include "orbita/RemoteOps.h"

#include "orbita/DiskApi.h"

namespace orbita {

bool DiskOps::upload(const QString& remotePath, const QString& localFile)
{
    return DiskApi::uploadBlocking(m_token, remotePath, localFile);
}

bool DiskOps::makeDir(const QString& remotePath)
{
    return DiskApi::mkdirBlocking(m_token, remotePath);
}

bool DiskOps::removeToTrash(const QString& remotePath)
{
    return DiskApi::removeBlocking(m_token, remotePath, /*permanently=*/false);
}

bool DiskOps::move(const QString& from, const QString& to)
{
    return DiskApi::moveBlocking(m_token, from, to);
}

QString DiskOps::remoteMd5(const QString& remotePath)
{
    Resource remote;
    if (!DiskApi::statBlocking(m_token, remotePath, &remote))
        return {};
    return remote.md5;
}

bool DiskOps::capacity(qint64* total, qint64* used)
{
    QMutexLocker lock(&m_capacityMutex);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool fresh = m_capacityFetchedAt.isValid()
        && m_capacityFetchedAt.secsTo(now) < 60;

    if (!fresh) {
        qint64 totalSpace = 0;
        qint64 usedSpace = 0;
        if (DiskApi::capacityBlocking(m_token, &totalSpace, &usedSpace)) {
            m_totalSpace = totalSpace;
            m_usedSpace = usedSpace;
            m_capacityFetchedAt = now;
        }
    }

    if (m_totalSpace <= 0)
        return false;

    if (total)
        *total = m_totalSpace;
    if (used)
        *used = m_usedSpace;
    return true;
}

} // namespace orbita
