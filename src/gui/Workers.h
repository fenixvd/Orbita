#pragma once

#include <QObject>
#include <QString>
#include <atomic>

namespace orbita {

class FuseMount;

/// Обход дерева Диска в отдельном потоке.
///
/// Живёт в своём QThread со своей очередью событий: сетевые вызовы блокирующие,
/// и в главном потоке они заморозили бы интерфейс.
class SyncWorker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

public Q_SLOTS:
    void run(const QString& token, const QString& root);
    void cancel() { m_cancelled = true; }
    /// Объём Диска — отдельно от обхода, он быстрый и нужен сразу после входа.
    void fetchCapacity(const QString& token);

    /// Дешёвая проверка новинок: один запрос последних загруженных файлов.
    /// Ловит то, что добавили с телефона, не перетряхивая весь Диск.
    void pollChanges(const QString& token);

    /// Перечитывает одну папку. В отличие от опроса новинок, замечает
    /// удаления и переименования — но только там, куда пользователь смотрит.
    void refreshDir(const QString& token, const QString& path);

Q_SIGNALS:
    void progress(int dirs, int files);
    void finished(bool ok, const QString& error);
    void capacity(qint64 total, qint64 used);
    /// Что-то в дереве изменилось — спискам пора перечитаться.
    void treeChanged();

private:
    bool m_cancelled = false;
};

/// Поток, внутри которого крутится файловая система.
///
/// Всё своё: соединение с базой, кэш, операции. Так поток FUSE ничего
/// не делит с интерфейсом и не нарушает привязку SQLite к потоку.
class MountWorker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    /// Зовётся из потока интерфейса, пока рабочий поток занят циклом FUSE,
    /// поэтому это обычный метод, а не слот: очередь сообщений тут не сработает.
    void requestStop();

public Q_SLOTS:
    void run(const QString& token, const QString& mountPoint);

Q_SIGNALS:
    void mounted();
    void stopped(const QString& error);

private:
    std::atomic<FuseMount*> m_mount { nullptr };
};

} // namespace orbita
