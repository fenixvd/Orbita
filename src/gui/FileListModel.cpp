#include "FileListModel.h"

#include "orbita/Paths.h"

#include <QLocale>
#include <algorithm>
#include <QMimeDatabase>
#include <QMimeType>

namespace orbita {

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_ready = m_store.open(paths::databaseFile());
    refresh();
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
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
    case SizeRole:
        return r.size;
    case SizeTextRole:
        return r.isDir ? QString() : QLocale().formattedDataSize(r.size);
    case IconNameRole: {
        if (r.isDir)
            return QStringLiteral("folder");
        // Тип определяем по имени: содержимого у заглушки нет и не должно быть,
        // заглядывать внутрь ради иконки — значит скачать весь файл.
        static QMimeDatabase mimeDb;
        const QMimeType type = mimeDb.mimeTypeForFile(r.name, QMimeDatabase::MatchExtension);
        const QString icon = type.iconName();
        return icon.isEmpty() ? QStringLiteral("text-x-generic") : icon;
    }
    case PublicUrlRole:
        return r.publicUrl;
    case PreviewableRole: {
        if (r.isDir)
            return false;
        // Превью Яндекс делает только для графических форматов. Определяем
        // по имени: заглядывать внутрь файла ради этого нельзя.
        static QMimeDatabase mimeDb;
        return mimeDb.mimeTypeForFile(r.name, QMimeDatabase::MatchExtension)
            .name()
            .startsWith(QLatin1String("image/"));
    }
    case Md5Role:
        return r.md5;
    case ModifiedRole:
        return r.modified.isValid() ? QLocale().toString(r.modified.toLocalTime(),
                                                         QLocale::ShortFormat)
                                    : QString();
    case StateRole:
        return static_cast<int>(r.state);
    case StateTextRole:
        if (r.isDir)
            return QString();
        switch (r.state) {
        case ContentState::Placeholder:
            return tr("в облаке");
        case ContentState::Cached:
            return tr("на устройстве");
        case ContentState::Pinned:
            return tr("закреплён");
        case ContentState::Dirty:
            return tr("ожидает выгрузки");
        }
        return QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { PathRole, "path" },
        { IsDirRole, "isDir" },
        { SizeRole, "size" },
        { SizeTextRole, "sizeText" },
        { StateRole, "state" },
        { StateTextRole, "stateText" },
        { IconNameRole, "iconName" },
        { PublicUrlRole, "publicUrl" },
        { PreviewableRole, "previewable" },
        { Md5Role, "md5" },
        { ModifiedRole, "modifiedText" },
    };
}

void FileListModel::cd(const QString& path)
{
    if (path.isEmpty())
        return;
    // Переход по папке всегда выводит из поиска — иначе непонятно,
    // что именно показано на экране.
    if (!m_search.isEmpty()) {
        m_search.clear();
        Q_EMIT searchChanged();
    } else if (path == m_path) {
        return;
    }
    m_path = path;
    Q_EMIT pathChanged();
    refresh();
}

void FileListModel::up()
{
    if (atRoot())
        return;
    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    cd(slash <= 0 ? QStringLiteral("/") : m_path.left(slash));
}

void FileListModel::setSearch(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed == m_search)
        return;
    m_search = trimmed;
    Q_EMIT searchChanged();
    refresh();
}

void FileListModel::setFoldersOnly(bool enabled)
{
    if (m_foldersOnly == enabled)
        return;
    m_foldersOnly = enabled;
    Q_EMIT foldersOnlyChanged();
    refresh();
}

void FileListModel::setSortBy(int mode)
{
    if (m_sortBy == mode)
        return;
    m_sortBy = mode;
    Q_EMIT sortChanged();
    refresh();
}

void FileListModel::setSortDescending(bool descending)
{
    if (m_sortDescending == descending)
        return;
    m_sortDescending = descending;
    Q_EMIT sortChanged();
    refresh();
}

void FileListModel::refresh()
{
    beginResetModel();
    if (!m_ready)
        m_items.clear();
    else if (searching())
        m_items = m_store.search(m_search);
    else
        m_items = m_store.children(m_path);

    if (m_foldersOnly) {
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                     [](const Resource& r) { return !r.isDir; }),
                      m_items.end());
    }

    // Папки всегда сверху, каким бы ни был порядок, — иначе в них не попасть,
    // не пролистав файлы.
    std::sort(m_items.begin(), m_items.end(), [this](const Resource& a, const Resource& b) {
        if (a.isDir != b.isDir)
            return a.isDir;

        bool less = false;
        switch (m_sortBy) {
        case SortBySize:
            less = a.size < b.size;
            break;
        case SortByDate:
            less = a.modified < b.modified;
            break;
        default:
            less = QString::localeAwareCompare(a.name, b.name) < 0;
            break;
        }
        return m_sortDescending ? !less : less;
    });
    endResetModel();
}

} // namespace orbita
