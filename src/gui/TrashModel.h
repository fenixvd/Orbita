#pragma once

#include "orbita/Types.h"

#include <QAbstractListModel>
#include <QQmlEngine>

namespace orbita {

/// Содержимое Корзины. В отличие от дерева Диска, локально не хранится —
/// список приходит из сети и живёт только в памяти.
class TrashModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Модель отдаётся из AppController")
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeTextRole,
        IconNameRole,
        OriginPathRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool empty() const { return m_items.isEmpty(); }
    void setItems(const QVector<Resource>& items);

Q_SIGNALS:
    void countChanged();

private:
    QVector<Resource> m_items;
};

} // namespace orbita
