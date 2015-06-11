#include "file-util.hpp"
#include <qtaround/error.hpp>
#include <qtaround/os.hpp>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>

namespace error = qtaround::error;
namespace os = qtaround::os;

QString path_normalize(QString const &path)
{
    auto res = path.simplified();
    auto pos = res.size();
    while (--pos) {
        if (res[pos] != QChar('/'))
            break;
    }
    return (pos + 1 == res.size()) ? res : res.left(pos);
}

static void get_file_times(QString const &path, struct timespec *times)
{
    struct stat stat;
    auto data = path.toLocal8Bit();
    int rc = ::stat(data.data(), &stat);
    if (rc < 0)
        raise_std_error({{"msg", "Can't get stat"}, {"path", path}});

    ::memcpy(&times[0], &stat.st_atime, sizeof(times[0]));
    ::memcpy(&times[1], &stat.st_mtime, sizeof(times[1]));
}

static void copy_utime(int fd, QFileInfo const &src)
{
    struct timespec times[2];
    get_file_times(src.filePath(), times);
    int rc = ::futimens(fd, times);
    if (rc < 0)
        raise_std_error({{"msg", "Can't change time"}, {"target", fd}});
}

void copy_utime(QFile const &fd, QFileInfo const &src)
{
    return copy_utime(fd.handle(), src);
}

void copy_utime(QString const &target, QFileInfo const &src)
{
    struct timespec times[2];
    get_file_times(src.filePath(), times);
    auto tgt_cs = target.toLocal8Bit();
    int rc = ::utimensat(AT_FDCWD, tgt_cs.data(), times, AT_SYMLINK_NOFOLLOW);
    if (rc < 0)
        raise_std_error({{"msg", "Can't change time"}, {"target", target}});
}

void mkdir(QString const &path, FileMode)
{
    // TODO use path
    auto parent = QFileInfo(path).dir();
    if (parent.exists()) {
        parent.mkdir(os::path::baseName(path));
    }
}

QFileInfo mkdir_similar(QFileInfo const &from, QFileInfo const &parent)
{
    if (!parent.exists())
        error::raise({{"msg", "No parent dir"}, {"parent", str(parent)}});

    auto dst_path = path(parent.filePath(), os::path::baseName(from.filePath()));
    debug::debug("mkdir", dst_path, " similar to ", str(from));
    QFileInfo dst_stat(dst_path);
    if (!dst_stat.exists()) {
        mkdir(dst_path, from.permissions());
        dst_stat.refresh();
    } else if (dst_stat.isDir()) {
        debug::debug("Already exists", dst_path);
    } else {
        error::raise({{"msg", "Destination type is different"}
                , {"src", str(from)}, {"parent", str(parent)}
                , {"dst", str(dst_stat)}});
    }
    return std::move(dst_stat);
}

void unlink(QString const &path)
{
    auto data = path.toLocal8Bit();
    auto rc = ::unlink(data.data());
    if (rc < 0)
        raise_std_error({{"msg", "Can't unlink"}, {"path", path}});
}

void copy(FileHandle const &dst, FileHandle const &src, size_t size, off_t off)
{
    auto p_src = mmap_create(nullptr, size, PROT_READ
                                 , MAP_PRIVATE, src->handle(), off);
    auto p_dst = mmap_create(nullptr, size, PROT_READ | PROT_WRITE
                             , MAP_SHARED, dst->handle(), off);
    memcpy(mmap_ptr(p_dst), mmap_ptr(p_src), size);
}

void copy(FileHandle &dst, FileHandle &src, size_t left_size
          , ErrorCallback on_error)
{
    if (!dst->resize(left_size))
        on_error({{"msg", "Can't resize"}, {"size", (unsigned)left_size}});

    static size_t max_chunk_size = 1024 * 1024;
    off_t off = 0;
    size_t size = left_size;
    dst->resize(left_size);
    while (left_size) {
        size = (left_size > max_chunk_size) ? max_chunk_size : left_size;
        copy(dst, src, size, off);
        left_size -= size;
        off += size;
    }
}

mode_t toPosixMode(QFileDevice::Permissions perm)
{
    return (perm & QFileDevice::ReadOwner? 0600 : 0)
        | (perm & QFileDevice::WriteOwner? 0400 : 0)
        | (perm & QFileDevice::ExeOwner? 0200 : 0)
        | (perm & QFileDevice::ReadGroup? 060 : 0)
        | (perm & QFileDevice::WriteGroup? 040 : 0)
        | (perm & QFileDevice::ExeGroup? 020 : 0)
        | (perm & QFileDevice::ReadOther? 06 : 0)
        | (perm & QFileDevice::WriteOther? 04 : 0)
        | (perm & QFileDevice::ExeOther? 02 : 0);
}

FileHandle copy_data(QString const &dst_path
                     , QFileInfo const &from, FileMode *pmode)
{
    debug::debug("Copy file data: ", str(from), "->", dst_path);
    auto src = std::make_shared<QFile>(from.filePath());
    if (!src->open(QIODevice::ReadOnly))
        error::raise({{"msg", "Cant' open src file"}, {"stat", str(from)}});
    auto raise_dst_error = [&dst_path](QVariantMap const &info) {
        error::raise(map({{"dst", dst_path}
                    , {"error", ::strerror(errno)}}), info);
    };
    auto dst = std::make_shared<QFile>(dst_path);
    auto flags = O_RDWR | O_CREAT;
    auto dst_cs = dst_path.toLocal8Bit();
    auto fd = (pmode
               ? ::open(dst_cs.data(), flags, toPosixMode(*pmode))
               : ::open(dst_cs.data(), flags));
    if (!dst->open(fd, QIODevice::ReadWrite))
        raise_dst_error({{"msg", "Cant' open dst file"}});
    using namespace std::placeholders;
    copy(dst, src, from.size(), std::bind(raise_dst_error, _1));
    return std::move(dst);
}


FileHandle rewrite(QString const &dst_path
                   , QString const &text
                   , FileMode mode)
{
    auto flags = O_CREAT | O_TRUNC | O_WRONLY;
    auto fd = ::open(dst_path.toLocal8Bit(), flags, mode);
    auto dst = std::make_shared<QFile>(dst_path);
    if (!dst->open(fd, QIODevice::WriteOnly))
        error::raise({{"msg", "Can't open dst"}, {"path", dst_path}});
    auto written = dst->write(text.toLocal8Bit(), text.size());
    if (written != (long)text.size())
        error::raise({{"msg", "Error writing"}
                , {"error", ::strerror(errno)}
                , {"path", dst_path}
                , {"data", text}
                , {"res", written}});
    return std::move(dst);
}

QString read_text(QString const &src_path)
{
    QFile f(src_path);
    if (!f.open(QIODevice::ReadOnly))
        error::raise({{"msg", "Can't open"}, {"path", src_path}});
    return QString::fromUtf8(f.readAll());
}

void symlink(QString const &tgt, QString const &link)
{
    auto tgt_cs = tgt.toLocal8Bit();
    auto link_cs = link.toLocal8Bit();
    auto rc = ::symlink(tgt_cs.data(), link_cs.data());
    if (rc < 0)
        raise_std_error({{"msg", "Can't create link"}
                , {"tgt", tgt}, {"link", link}});
}

QString readlink(QString const &path)
{
    auto path_cs = path.toLocal8Bit();
    char buf[PATH_MAX];
    auto rc = ::readlink(path_cs, buf, sizeof(buf));
    if (rc < 0)
        raise_std_error({{"msg", "Can't read link"}, {"link", path}});
    if (rc == sizeof(buf))
        error::raise({{"msg", "Small buf reading link"}, {"path", path}});
    buf[rc] = '\0';
    return QString::fromLocal8Bit(buf);
}
