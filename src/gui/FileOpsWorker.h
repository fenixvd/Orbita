#pragma once

#include "orbita/Types.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace orbita {

/// Все изменяющие операции над Диском в отдельном потоке.
///
/// Каждая из них ходит в сеть, поэтому в главном потоке любая заморозила бы
/// окно. Наружу отдаются только сигналы о результате.
class FileOpsWorker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

public Q_SLOTS:
    /// Слот, а не обычный метод: токен приходит из потока интерфейса
    /// после входа, а пользуется им рабочий поток.
    void setToken(const QString& token) { m_token = token; }

    /// Загружает файлы (и папки целиком) в указанную папку Диска.
    void upload(const QStringList& localPaths, const QString& destDir);
    /// Просит Яндекс скачать файл по ссылке прямо на Диск.
    void uploadFromUrl(const QString& sourceUrl, const QString& destDir, const QString& name);
    void createFolder(const QString& path);
    void rename(const QString& from, const QString& to);
    void moveTo(const QStringList& paths, const QString& destDir);
    void removeToTrash(const QString& path);

    /// Создаёт копию рядом с исходником — целиком силами Яндекса,
    /// без скачивания и повторной заливки.
    void duplicate(const QString& path);

    /// Пустой пароль и нулевой срок означают обычную ссылку.
    void publish(const QString& path, const QString& password = {}, qint64 availableUntil = 0);
    void unpublish(const QString& path);

    /// Скачивает файл целиком и закрепляет его офлайн.
    void pin(const QString& path);
    void unpin(const QString& path);

    /// Дозаливает всё, что осталось помеченным как изменённое.
    ///
    /// Без этого сорванная выгрузка теряется навсегда: файл лежит в кэше
    /// со значком «ждёт выгрузки», но повторить попытку некому.
    void retryPending();

    void loadTrash();
    void restoreFromTrash(const QString& trashPath);
    void deleteFromTrash(const QString& trashPath);
    void emptyTrash();

Q_SIGNALS:
    /// Прогресс длительной операции: подпись и доля от 0 до 1 (-1 — неизвестна).
    void progress(const QString& caption, qreal fraction);
    /// Операция завершена: сообщение показывается пользователю.
    void done(bool ok, const QString& message);
    /// Дерево изменилось — спискам пора перечитаться.
    void treeChanged();
    void trashLoaded(const QVector<orbita::Resource>& items);
    void published(const QString& path, const QString& url);

private:
    /// Общая часть закачки: одна запись (файл или папка) целиком.
    bool uploadEntry(const QString& localPath, const QString& destDir, int* uploaded, int total);
    int countFiles(const QString& localPath) const;

    QString m_token;
};

} // namespace orbita
