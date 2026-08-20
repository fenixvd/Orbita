#pragma once

#include "orbita/Types.h"

#include <QObject>
#include <QVector>

class QNetworkAccessManager;

namespace orbita {

/// Тонкий клиент REST API Яндекс.Диска (cloud-api.yandex.net/v1/disk).
///
/// Дельта-эндпоинта у API нет, поэтому синхронизация строится на опросе:
/// список папки + сравнение md5/modified с тем, что уже в MetadataStore.
class DiskApi : public QObject {
    Q_OBJECT
public:
    explicit DiskApi(QObject* parent = nullptr);

    void setToken(const QString& token) { m_token = token; }

    /// Содержимое папки. limit ограничен API, большие папки читаются постранично.
    void list(const QString& path, int limit = 200, int offset = 0);

    /// Синхронная загрузка файла — её зовёт FUSE-поток, которому и положено ждать.
    /// Создаёт собственный QNetworkAccessManager, поэтому безопасна вне главного потока.
    static bool downloadBlocking(const QString& token,
                                 const QString& remotePath,
                                 const QString& destFile,
                                 const std::function<void(qint64, qint64)>& onProgress = {});

    /// Заливка файла: ссылка у загрузчика живёт 30 минут, сам PUT идёт без токена.
    static bool uploadBlocking(const QString& token,
                               const QString& remotePath,
                               const QString& localFile,
                               const std::function<void(qint64, qint64)>& onProgress = {});

    /// Ссылка на содержимое файла. Живёт около получаса, поэтому кэшируется:
    /// иначе каждое чтение стоило бы двух запросов вместо одного.
    static QString contentHref(const QString& token, const QString& remotePath);

    /// Читает кусок файла, не скачивая его целиком.
    ///
    /// Это и есть суть «файлов по требованию»: определение типа файла или
    /// чтение заголовка стоит килобайтов, а не всего файла.
    /// Возвращает число прочитанных байт или -1 при ошибке.
    static qint64 readRangeBlocking(const QString& token,
                                    const QString& remotePath,
                                    qint64 offset,
                                    qint64 size,
                                    char* buffer);

    /// Объём Диска в байтах: всего, занято и в корзине.
    static bool capacityBlocking(const QString& token,
                                 qint64* totalSpace,
                                 qint64* usedSpace,
                                 qint64* trashSize = nullptr);

    /// Загрузка на Диск по внешней ссылке: файл качает сам Яндекс,
    /// через компьютер пользователя не проходит ни байта.
    static bool uploadFromUrlBlocking(const QString& token,
                                      const QString& remotePath,
                                      const QString& sourceUrl);

    static bool mkdirBlocking(const QString& token, const QString& remotePath);
    /// По умолчанию — в корзину, а не насовсем.
    static bool removeBlocking(const QString& token,
                               const QString& remotePath,
                               bool permanently = false);
    static bool moveBlocking(const QString& token,
                             const QString& from,
                             const QString& to,
                             bool overwrite = true);

    /// Последние загруженные на Диск файлы.
    ///
    /// Дешёвый способ узнать о новинках с других устройств: один запрос
    /// вместо обхода всего дерева. Удаления так не заметить — для них
    /// перечитывается конкретная папка.
    static bool lastUploadedBlocking(const QString& token,
                                     QVector<Resource>* items,
                                     int limit = 50);

    /// Содержимое корзины. Пути здесь свои, из пространства корзины.
    static bool trashListBlocking(const QString& token,
                                  QVector<Resource>* items,
                                  int limit = 200,
                                  int offset = 0);
    /// Возвращает файл из корзины на прежнее место.
    static bool trashRestoreBlocking(const QString& token, const QString& trashPath);
    /// Удаляет из корзины безвозвратно. Пустой путь — очистить корзину целиком.
    static bool trashDeleteBlocking(const QString& token, const QString& trashPath = {});

    /// Метаданные одного ресурса — нужны, чтобы перед перезаписью убедиться,
    /// что файл на Диске не изменился с тех пор, как мы его скачали.
    static bool statBlocking(const QString& token, const QString& remotePath, Resource* out);

    /// Копирование внутри Диска — целиком на стороне Яндекса, без скачивания.
    static bool copyBlocking(const QString& token,
                             const QString& from,
                             const QString& to,
                             bool overwrite = false);

    /// Публикует ресурс и возвращает публичную ссылку.
    ///
    /// `password` и `availableUntil` (срок жизни ссылки в секундах) можно
    /// не задавать — тогда ссылка обычная, бессрочная и без пароля.
    static QString publishBlocking(const QString& token,
                                   const QString& remotePath,
                                   const QString& password = {},
                                   qint64 availableUntil = 0);
    /// Закрывает публичный доступ.
    static bool unpublishBlocking(const QString& token, const QString& remotePath);

    /// "disk:/Фото/кот.jpg" -> "/Фото/кот.jpg"
    static QString normalizePath(const QString& apiPath);

Q_SIGNALS:
    void listed(const QString& path, const QVector<orbita::Resource>& items, bool hasMore);
    void errorOccurred(const QString& message);

private:
    QNetworkAccessManager* m_net = nullptr;
    QString m_token;
};

} // namespace orbita
