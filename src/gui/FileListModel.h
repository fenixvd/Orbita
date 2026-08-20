#pragma once

#include "orbita/MetadataStore.h"

#include <QAbstractListModel>
#include <QQmlEngine>

namespace orbita {

/// Содержимое одной папки Диска для списка в интерфейсе.
///
/// Данные берутся из локальной базы, поэтому переход по папкам мгновенный
/// и работает без сети — ровно как в файловом менеджере поверх монтирования.
class FileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY pathChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    /// Показывать только папки — режим для выбора места назначения.
    Q_PROPERTY(bool foldersOnly READ foldersOnly WRITE setFoldersOnly NOTIFY foldersOnlyChanged)
    Q_PROPERTY(int sortBy READ sortBy WRITE setSortBy NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeRole,
        SizeTextRole,
        StateRole,
        StateTextRole,
        IconNameRole,
        PublicUrlRole,
        PreviewableRole,
        Md5Role,
        ModifiedRole,
    };

    /// Порядок в списке. Хранится в модели, а не в представлении: сортировать
    /// десять тысяч записей в QML было бы заметно медленнее.
    enum SortBy {
        SortByName,
        SortBySize,
        SortByDate,
    };
    Q_ENUM(SortBy)

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    bool atRoot() const { return m_path == QLatin1String("/"); }

    Q_INVOKABLE void cd(const QString& path);
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();

    /// Поиск по всему дереву. Пустая строка возвращает обычный показ папки.
    Q_INVOKABLE void setSearch(const QString& query);
    QString searchQuery() const { return m_search; }
    bool foldersOnly() const { return m_foldersOnly; }
    void setFoldersOnly(bool enabled);

    int sortBy() const { return m_sortBy; }
    void setSortBy(int mode);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    bool searching() const { return !m_search.isEmpty(); }

Q_SIGNALS:
    void pathChanged();
    void searchChanged();
    void foldersOnlyChanged();
    void sortChanged();

private:
    MetadataStore m_store;
    QString m_path = QStringLiteral("/");
    QString m_search;
    bool m_foldersOnly = false;
    int m_sortBy = SortByName;
    bool m_sortDescending = false;
    QVector<Resource> m_items;
    bool m_ready = false;
};

} // namespace orbita
