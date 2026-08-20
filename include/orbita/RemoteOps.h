#pragma once

#include <QDateTime>
#include <QMutex>
#include <QString>

namespace orbita {

/// Изменяющие операции над Диском.
///
/// Отдельно от ContentProvider, чтобы монтирование можно было поднять
/// только для чтения — просто не передав реализацию.
class RemoteOps {
public:
    virtual ~RemoteOps() = default;

    virtual bool upload(const QString& remotePath, const QString& localFile) = 0;
    virtual bool makeDir(const QString& remotePath) = 0;
    /// В корзину, а не безвозвратно: пользователь должен иметь право передумать.
    virtual bool removeToTrash(const QString& remotePath) = 0;
    virtual bool move(const QString& from, const QString& to) = 0;

    /// Объём Диска в байтах. false — сведений нет, вызывающий покажет заглушку.
    virtual bool capacity(qint64* total, qint64* used) = 0;

    /// Контрольная сумма файла на Диске. Пустая строка — файла нет либо
    /// узнать не удалось. Нужна, чтобы не затереть чужие изменения.
    virtual QString remoteMd5(const QString& remotePath) = 0;

    virtual QString lastError() const { return {}; }
};

/// Реализация поверх REST API. Все вызовы блокирующие — их делает поток FUSE.
class DiskOps : public RemoteOps {
public:
    explicit DiskOps(QString token)
        : m_token(std::move(token))
    {
    }

    bool upload(const QString& remotePath, const QString& localFile) override;
    bool makeDir(const QString& remotePath) override;
    bool removeToTrash(const QString& remotePath) override;
    bool move(const QString& from, const QString& to) override;
    bool capacity(qint64* total, qint64* used) override;
    QString remoteMd5(const QString& remotePath) override;

private:
    QString m_token;

    /// Свободное место запрашивают часто (файловый менеджер зовёт statfs
    /// на каждый чих), а меняется оно медленно — держим ответ около минуты.
    mutable QMutex m_capacityMutex;
    mutable qint64 m_totalSpace = 0;
    mutable qint64 m_usedSpace = 0;
    mutable QDateTime m_capacityFetchedAt;
};

} // namespace orbita
