#include "TrashModel.h"

#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>

namespace orbita {

int TrashModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant TrashModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const Resource& r = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return r.name;
    case PathRole:
        return r.path;
    case IsDirRole:
        return r.isDir;
    case SizeTextRole:
        return r.isDir ? tr("папка") : QLocale().formattedDataSize(r.size);
    case OriginPathRole:
        return r.originPath;
    case IconNameRole: {
        if (r.isDir)
            return QStringLiteral("folder");
        static QMimeDatabase mimeDb;
        const QString icon = mimeDb.mimeTypeForFile(r.name, QMimeDatabase::MatchExtension)
                                 .iconName();
        return icon.isEmpty() ? QStringLiteral("text-x-generic") : icon;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TrashModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { PathRole, "path" },
        { IsDirRole, "isDir" },
        { SizeTextRole, "sizeText" },
        { IconNameRole, "iconName" },
        { OriginPathRole, "originPath" },
    };
}

void TrashModel::setItems(const QVector<Resource>& items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace orbita
