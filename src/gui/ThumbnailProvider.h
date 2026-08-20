#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QThread>
#include <QWaitCondition>

class QNetworkAccessManager;

namespace orbita {

/// Сетевая часть миниатюр: живёт в своём потоке и работает без блокировок.
///
/// Запросы ведутся сигналами, а не ожиданием в цикле событий. Это
/// принципиально: вложенный QEventLoop в потоке загрузки картинок заново
/// разбирал очередь событий и рекурсивно вызывал следующую загрузку,
/// пока не переполнялся стек.
class ThumbnailFetcher : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailFetcher(QObject* parent = nullptr);

public Q_SLOTS:
    void start(quint64 id, const QString& diskPath, int box, const QString& token);

Q_SIGNALS:
    void finished(quint64 id, const QImage& image);

private:
    void requestPreviewLink(quint64 id, const QString& diskPath, int box, const QString& token);
    void requestImageData(quint64 id, const QString& url, const QString& token);

    QNetworkAccessManager* m_net = nullptr;
};

/// Миниатюры файлов Диска для интерфейса.
///
/// Главное: превью отдаёт сам Яндекс отдельной ссылкой, поэтому эскиз
/// картинки виден без загрузки самого файла — ровно то, чего не может
/// обычный файловый менеджер поверх монтирования.
///
/// В QML используется как image://orbita/<путь на Диске>.
class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider();
    ~ThumbnailProvider() override;

    /// Вызывается из потока загрузки изображений QML. Ждёт результата
    /// на условной переменной — очередь событий при этом не крутится,
    /// поэтому повторного входа сюда не происходит.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QString token();
    QString diskCachePath(const QString& diskPath, int box) const;

    QThread m_thread;
    ThumbnailFetcher* m_fetcher = nullptr;

    QMutex m_mutex;
    QWaitCondition m_ready;
    QHash<quint64, QImage> m_results;
    quint64 m_nextId = 1;
    QString m_token;
};

} // namespace orbita
