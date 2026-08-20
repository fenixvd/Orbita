#include "orbita/FuseMount.h"

#include "orbita/ContentProvider.h"
#include "orbita/MetadataStore.h"
#include "orbita/Naming.h"
#include "orbita/RemoteOps.h"

// 312 = libfuse 3.12: с этой версии у многопоточного цикла настраиваемое
// число потоков (fuse_loop_cfg_*), что нам и нужно.
#define FUSE_USE_VERSION 312
#include <fuse.h>
#include <fuse_lowlevel.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QFileInfo>
#include <QList>
#include <QVarLengthArray>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace orbita {
namespace {

/// Всё, что нужно колбэкам FUSE. Кладётся в private_data контекста.
struct FuseContext {
    MetadataStore* meta = nullptr;
    ContentProvider* content = nullptr;
    RemoteOps* ops = nullptr; ///< nullptr — монтирование только для чтения
};

/// Открытый файл: путь нужен при закрытии, чтобы решить, выгружать ли.
struct Handle {
    int fd = -1;
    QString path;
    bool dirty = false;
    /// Новый файл заводим на Диске даже пустым, но ровно один раз.
    bool isNew = false;
};

FuseContext* ctx()
{
    return static_cast<FuseContext*>(fuse_get_context()->private_data);
}

QString toPath(const char* path)
{
    return QString::fromUtf8(path);
}

Handle* handleOf(struct fuse_file_info* fi)
{
    return reinterpret_cast<Handle*>(static_cast<uintptr_t>(fi->fh));
}

bool writable()
{
    return ctx()->ops != nullptr;
}

/// Заносит новый узел в метаданные, чтобы файл сразу был виден в папке.
void recordNode(const QString& path, bool isDir, qint64 size, ContentState state)
{
    Resource r;
    r.path = path;
    r.name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    r.isDir = isDir;
    r.size = size;
    r.modified = QDateTime::currentDateTimeUtc();
    r.state = state;
    ctx()->meta->upsert(r);
}

/// Выгружает правки на Диск. Общая точка для flush и fsync.
int uploadIfDirty(Handle* h)
{
    if (!h || (!h->dirty && !h->isNew))
        return 0;
    if (!writable())
        return -EROFS;

    // Данные могли остаться в буфере ядра — до заливки их надо дописать.
    ::fsync(h->fd);

    const QString local = ctx()->content->localFile(h->path);
    if (local.isEmpty())
        return -EIO;

    // Если файл на Диске успели изменить, поверх писать нельзя: чужая правка
    // исчезла бы молча. Кладём свою версию рядом.
    QString target = h->path;
    const auto known = ctx()->meta->get(h->path);
    if (known && !known->md5.isEmpty()) {
        const QString actual = ctx()->ops->remoteMd5(h->path);
        if (!actual.isEmpty() && actual != known->md5)
            target = naming::conflictName(h->path);
    }

    if (!ctx()->ops->upload(target, local))
        return -EIO;

    h->dirty = false;
    h->isNew = false;

    if (target != h->path) {
        // Наше ушло в копию, а исходный сбрасываем в заглушку — иначе при
        // чтении подсунем устаревший кэш вместо чужой версии.
        ctx()->meta->setState(h->path, ContentState::Placeholder);
        recordNode(target, /*isDir=*/false, QFileInfo(local).size(), ContentState::Cached);
        return 0;
    }

    ctx()->meta->setSize(h->path, QFileInfo(local).size());
    ctx()->meta->setState(h->path, ContentState::Cached);
    return 0;
}

int orbita_getattr(const char* path, struct stat* st, struct fuse_file_info*)
{
    std::memset(st, 0, sizeof(struct stat));
    const QString p = toPath(path);

    st->st_uid = fuse_get_context()->uid;
    st->st_gid = fuse_get_context()->gid;

    // Корень в базе не хранится — он существует всегда.
    if (p == QLatin1String("/")) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_mtime = st->st_ctime = st->st_atime = ::time(nullptr);
        return 0;
    }

    const auto res = ctx()->meta->get(p);
    if (!res)
        return -ENOENT;

    if (res->isDir) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        // Размер настоящий, хотя байтов локально нет: для программ заглушка
        // должна быть неотличима от файла.
        st->st_size = res->size;
        // st_blocks = 0 — так `du` честно покажет, что место не занято.
        st->st_blocks = 0;

        // Пока идёт запись, истина локальная: размер должен расти.
        if (res->state == ContentState::Dirty) {
            const QFileInfo local(ctx()->content->localFile(p));
            if (local.exists()) {
                st->st_size = local.size();
                st->st_blocks = (local.size() + 511) / 512;
            }
        } else if (ctx()->content->isMaterialized(p)) {
            st->st_blocks = (res->size + 511) / 512;
        }
    }

    const time_t mtime = res->modified.isValid() ? res->modified.toSecsSinceEpoch() : 0;
    st->st_mtime = mtime;
    st->st_ctime = mtime;
    st->st_atime = mtime;
    return 0;
}

int orbita_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t,
                   struct fuse_file_info*, enum fuse_readdir_flags)
{
    const QString p = toPath(path);
    if (p != QLatin1String("/") && !ctx()->meta->get(p))
        return -ENOENT;

    filler(buf, ".", nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_DEFAULTS);

    // Без единого запроса в сеть — содержимое папки уже в базе.
    for (const Resource& child : ctx()->meta->children(p)) {
        struct stat st {};
        st.st_mode = child.isDir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
        st.st_size = child.size;
        filler(buf, child.name.toUtf8().constData(), &st, 0, FUSE_FILL_DIR_DEFAULTS);
    }
    return 0;
}

int orbita_open(const char* path, struct fuse_file_info* fi)
{
    const QString p = toPath(path);
    const auto res = ctx()->meta->get(p);
    if (!res)
        return -ENOENT;
    if (res->isDir)
        return -EISDIR;

    const bool forWriting = (fi->flags & O_ACCMODE) != O_RDONLY;
    if (forWriting && !writable())
        return -EROFS;

    if (forWriting) {
        // Правка требует файла целиком: дописать кусок API не позволяет.
        const QString local = ctx()->content->prepareForWrite(p, (fi->flags & O_TRUNC) != 0);
        if (local.isEmpty())
            return -EIO;

        const int fd = ::open(QFile::encodeName(local).constData(), O_RDWR);
        if (fd < 0)
            return -errno;

        auto* h = new Handle { fd, p, false, false };
        fi->fh = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
        return 0;
    }

    // Чтение файл не скачивает: определение типа в файловом менеджере —
    // это первые килобайты, а тянуло бы гигабайты.
    int fd = -1;
    if (ctx()->content->isMaterialized(p)) {
        const QString local = ctx()->content->localFile(p);
        fd = ::open(QFile::encodeName(local).constData(), O_RDONLY);
    }

    auto* h = new Handle { fd, p, false, false };
    fi->fh = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
    return 0;
}

int orbita_create(const char* path, mode_t, struct fuse_file_info* fi)
{
    if (!writable())
        return -EROFS;

    const QString p = toPath(path);
    const QString local = ctx()->content->prepareForWrite(p, /*truncate=*/true);
    if (local.isEmpty())
        return -EIO;

    const int fd = ::open(QFile::encodeName(local).constData(), O_RDWR);
    if (fd < 0)
        return -errno;

    // Файл появляется в дереве сразу, ещё до выгрузки: иначе программа,
    // создавшая его, не увидит собственный файл.
    recordNode(p, /*isDir=*/false, 0, ContentState::Dirty);

    // dirty=false — пустышку заливать незачем; isNew доведёт файл до Диска,
    // даже если в него ничего не запишут.
    auto* h = new Handle { fd, p, false, true };
    fi->fh = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
    return 0;
}

int orbita_read(const char*, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    Handle* h = handleOf(fi);

    if (h->fd >= 0) {
        const ssize_t n = ::pread(h->fd, buf, size, offset);
        if (n < 0)
            return -errno;
        return static_cast<int>(n);
    }

    // Содержимого локально нет — берём ровно запрошенный кусок из сети.
    const qint64 n = ctx()->content->readRange(h->path, offset, static_cast<qint64>(size), buf);
    if (n < 0)
        return -EIO;
    return static_cast<int>(n);
}

int orbita_write(const char*, const char* buf, size_t size, off_t offset,
                 struct fuse_file_info* fi)
{
    if (!writable())
        return -EROFS;

    Handle* h = handleOf(fi);
    const ssize_t n = ::pwrite(h->fd, buf, size, offset);
    if (n < 0)
        return -errno;

    h->dirty = true;
    return static_cast<int>(n);
}

int orbita_truncate(const char* path, off_t size, struct fuse_file_info* fi)
{
    if (!writable())
        return -EROFS;

    const QString p = toPath(path);
    if (fi && fi->fh) {
        Handle* h = handleOf(fi);
        if (::ftruncate(h->fd, size) < 0)
            return -errno;
        h->dirty = true;
        return 0;
    }

    const QString local = ctx()->content->prepareForWrite(p, size == 0);
    if (local.isEmpty())
        return -EIO;
    if (::truncate(QFile::encodeName(local).constData(), size) < 0)
        return -errno;

    ctx()->meta->setSize(p, size);
    ctx()->meta->setState(p, ContentState::Dirty);
    return 0;
}

int orbita_flush(const char*, struct fuse_file_info* fi)
{
    // Именно здесь: ошибку flush ядро отдаёт из close(), а из release — некуда.
    return uploadIfDirty(handleOf(fi));
}

int orbita_fsync(const char*, int, struct fuse_file_info* fi)
{
    return uploadIfDirty(handleOf(fi));
}

int orbita_release(const char*, struct fuse_file_info* fi)
{
    Handle* h = handleOf(fi);
    if (!h)
        return 0;
    if (h->fd >= 0)
        ::close(h->fd);
    delete h;
    fi->fh = 0;
    return 0;
}

int orbita_mkdir(const char* path, mode_t)
{
    if (!writable())
        return -EROFS;

    const QString p = toPath(path);
    if (!ctx()->ops->makeDir(p))
        return -EIO;

    recordNode(p, /*isDir=*/true, 0, ContentState::Placeholder);
    return 0;
}

int orbita_unlink(const char* path)
{
    if (!writable())
        return -EROFS;

    const QString p = toPath(path);
    if (!ctx()->ops->removeToTrash(p))
        return -EIO;

    ctx()->meta->remove(p);
    return 0;
}

int orbita_rmdir(const char* path)
{
    if (!writable())
        return -EROFS;

    const QString p = toPath(path);
    if (!ctx()->ops->removeToTrash(p))
        return -EIO;

    ctx()->meta->removeRecursive(p);
    return 0;
}

int orbita_rename(const char* from, const char* to, unsigned int flags)
{
    if (!writable())
        return -EROFS;
    if (flags != 0)
        return -EINVAL; // RENAME_EXCHANGE/NOREPLACE у API нет

    const QString src = toPath(from);
    const QString dst = toPath(to);
    if (!ctx()->ops->move(src, dst))
        return -EIO;

    ctx()->content->renameLocal(src, dst);
    ctx()->meta->rename(src, dst);
    return 0;
}

/// Свободное место — квота Диска, а не локального раздела: копируют-то на Диск.
int orbita_statfs(const char*, struct statvfs* st)
{
    constexpr qint64 kBlockSize = 4096;

    qint64 total = 0;
    qint64 used = 0;
    if (!writable() || !ctx()->ops->capacity(&total, &used)) {
        // Не узнали — говорим «много»: иначе программы решат, что места нет.
        total = 1024LL * 1024 * 1024 * 1024;
        used = 0;
    }

    const qint64 free = qMax<qint64>(0, total - used);

    st->f_bsize = kBlockSize;
    st->f_frsize = kBlockSize;
    st->f_blocks = static_cast<fsblkcnt_t>(total / kBlockSize);
    st->f_bfree = static_cast<fsblkcnt_t>(free / kBlockSize);
    st->f_bavail = st->f_bfree;
    st->f_namemax = 255;
    return 0;
}

/// Времена меняются локально: у API нет способа задать mtime отдельно.
int orbita_utimens(const char*, const struct timespec[2], struct fuse_file_info*)
{
    return 0;
}

/// Менять права негде, но без согласия падает `cp -p`.
int orbita_chmod(const char*, mode_t, struct fuse_file_info*)
{
    return 0;
}

int orbita_chown(const char*, uid_t, gid_t, struct fuse_file_info*)
{
    return 0;
}

const struct fuse_operations kOps = [] {
    struct fuse_operations ops {};
    ops.getattr = orbita_getattr;
    ops.readdir = orbita_readdir;
    ops.open = orbita_open;
    ops.create = orbita_create;
    ops.read = orbita_read;
    ops.write = orbita_write;
    ops.truncate = orbita_truncate;
    ops.flush = orbita_flush;
    ops.fsync = orbita_fsync;
    ops.release = orbita_release;
    ops.mkdir = orbita_mkdir;
    ops.unlink = orbita_unlink;
    ops.rmdir = orbita_rmdir;
    ops.rename = orbita_rename;
    ops.statfs = orbita_statfs;
    ops.utimens = orbita_utimens;
    ops.chmod = orbita_chmod;
    ops.chown = orbita_chown;
    return ops;
}();

/// Создаёт каталог и убирает брошенное монтирование от прошлого запуска.
///
/// Брошенное отвечает на stat ошибкой ENOTCONN и папкой не считается — без
/// уборки клиент больше никогда не поднялся бы сам.
bool prepareMountPoint(const QString& mountPoint)
{
    const QByteArray raw = QFile::encodeName(mountPoint);

    struct stat st {};
    if (::stat(raw.constData(), &st) != 0 && errno == ENOTCONN) {
        // Ленивое отцепление: сработает, даже если каталог кто-то держит открытым.
        QProcess::execute(QStringLiteral("fusermount3"),
                          { QStringLiteral("-u"), QStringLiteral("-z"), mountPoint });
    }

    QDir().mkpath(mountPoint);
    return QFileInfo(mountPoint).isDir();
}

/// Номер соединения FUSE из /proc/self/mountinfo.
///
/// Не через stat по своей точке монтирования: тот ушёл бы запросом в нашу же
/// ФС, которая ещё никого не обслуживает, — процесс встал бы сам на себя.
int connectionIdOf(const QString& mountPoint)
{
    QFile mountinfo(QStringLiteral("/proc/self/mountinfo"));
    if (!mountinfo.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    // Формат: id parent major:minor корень точка ...
    // Пробелы в пути записаны как \040.
    const QString needle = QString(mountPoint).replace(QLatin1Char(' '), QLatin1String("\\040"));
    while (!mountinfo.atEnd()) {
        const QStringList fields = QString::fromUtf8(mountinfo.readLine()).split(QLatin1Char(' '));
        if (fields.size() < 5 || fields.at(4) != needle)
            continue;

        const QStringList device = fields.at(2).split(QLatin1Char(':'));
        if (device.size() == 2)
            return device.at(1).toInt();
    }
    return -1;
}

} // namespace

FuseMount::FuseMount(MetadataStore& meta, ContentProvider& content, RemoteOps* ops)
    : m_meta(meta)
    , m_content(content)
    , m_ops(ops)
{
}

bool FuseMount::run(const QString& mountPoint, bool debug)
{
    if (!prepareMountPoint(mountPoint)) {
        m_lastError = QStringLiteral("Не удалось подготовить точку монтирования: %1")
                          .arg(mountPoint);
        return false;
    }
    m_mountPoint = QFileInfo(mountPoint).absoluteFilePath();

    QList<QByteArray> argv { QByteArrayLiteral("orbita") };
    // subtype=orbita — в /proc/mounts мы fuse.orbita. По этой метке KDE считает
    // ФС медленной и не строит эскизы сам.
    argv.append(QByteArrayLiteral("-o"));
    argv.append(QByteArrayLiteral("subtype=orbita,fsname=Orbita"));
    // Кэш ядра на метаданные — иначе обход папки долбит базу на каждый файл.
    argv.append(QByteArrayLiteral("-o"));
    argv.append(QByteArrayLiteral("attr_timeout=30,entry_timeout=30"));
    if (debug)
        argv.append(QByteArrayLiteral("-d"));

    QVarLengthArray<char*, 8> argptrs;
    for (QByteArray& a : argv)
        argptrs.append(a.data());

    struct fuse_args args = FUSE_ARGS_INIT(argptrs.size(), argptrs.data());
    FuseContext context { &m_meta, &m_content, m_ops };

    struct fuse* handle = fuse_new(&args, &kOps, sizeof(kOps), &context);
    fuse_opt_free_args(&args);
    if (!handle) {
        m_lastError = QStringLiteral("Не удалось создать файловую систему");
        return false;
    }

    if (fuse_mount(handle, QFile::encodeName(m_mountPoint).constData()) != 0) {
        fuse_destroy(handle);
        m_lastError = QStringLiteral("Не удалось смонтировать %1").arg(m_mountPoint);
        return false;
    }

    m_fuse.store(handle);

    m_connectionId.store(connectionIdOf(m_mountPoint));

    // Без своих обработчиков Ctrl+C оставил бы точку монтирования висеть.
    struct fuse_session* session = fuse_get_session(handle);
    fuse_set_signal_handlers(session);

    // Пока один поток ждёт загрузку, остальные отвечают на getattr и readdir.
    struct fuse_loop_config* config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_max_threads(config, 8);
    const int rc = fuse_loop_mt(handle, config);
    fuse_loop_cfg_destroy(config);

    fuse_remove_signal_handlers(session);
    m_fuse.store(nullptr);
    m_connectionId.store(-1);
    fuse_unmount(handle);
    fuse_destroy(handle);

    if (rc != 0) {
        m_lastError = QStringLiteral("Цикл файловой системы завершился с кодом %1").arg(rc);
        return false;
    }
    return true;
}

void FuseMount::stop()
{
    struct fuse* handle = m_fuse.load();
    if (!handle)
        return;

    // Флаг выхода: потоки цикла завершатся, как только проснутся.
    fuse_exit(handle);

    // Ленивое: обычное получило бы «устройство занято», пока в папке Dolphin.
    QProcess::execute(QStringLiteral("fusermount3"),
                      { QStringLiteral("-u"), QStringLiteral("-z"), m_mountPoint });

    // Потоки всё ещё спят в ожидании запросов, которых больше не будет.
    // Разрыв соединения со стороны ядра — единственное, что их разбудит.
    const int connection = m_connectionId.load();
    if (connection >= 0) {
        QFile abort(QStringLiteral("/sys/fs/fuse/connections/%1/abort").arg(connection));
        if (abort.open(QIODevice::WriteOnly))
            abort.write("1");
    }
}

} // namespace orbita
