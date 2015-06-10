#include "file-util.hpp"
#include <qtaround/error.hpp>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>

namespace error = qtaround::error;

std::string readlink(std::string const &p)
{
    char buf[PATH_MAX + 1];
    auto s = ::readlink(p.c_str(), buf, sizeof(buf));
    if (s < 0) {
        auto err = errno;
        if (err == EINVAL)
            return p;
        auto err_str = ::strerror(err);
        error::raise({{"msg", "readlink error"}
                , {"error", err_str ? err_str : "?"}});
    }
    return std::string(s ? buf : "");
}

std::string path_normalize(std::string const &path)
{
    auto sz = path.size();
    if (sz > 0) {
        auto pos = path.find_last_not_of(" \t\n");
        if (pos == std::string::npos) {
            pos = sz - 1;
        }
        while (pos > 0) if (path[pos] == '/') --pos; else break;
        return pos == sz - 1 ? path : path.substr(0, pos + 1);
    } else {
        return path;
    }
}

StatBase::StatBase(std::string const &path)
    : path_(path_normalize(path))
{
    refresh();
}

void StatBase::refresh()
{
    auto rc = ::stat(path_.c_str(), &data_);
    type_ = (rc == 0) ? FileType::Unknown : FileType::Absent;
    if (type_ == FileType::Absent)
        err_ = errno;
}

FileType StatBase::file_type() const
{
    if (type_ == FileType::Unknown) {
        auto m = data_.st_mode;
        type_ = (S_ISREG(m)
                 ? FileType::File
                 : (S_ISDIR(m)
                    ? FileType::Dir
                    : (S_ISLNK(m)
                       ? FileType::Symlink
                       : (S_ISSOCK(m)
                          ? FileType::Socket
                          : (S_ISCHR(m)
                             ? FileType::Char
                             : (S_ISBLK(m)
                                ? FileType::Block
                                : (S_ISFIFO(m)
                                   ? FileType::Fifo
                                   : FileType::Unknown)))))));
        if (type_ == FileType::Unknown)
            error::raise({{"msg", "Unknown file type"}
                    , {"path", QString::fromStdString(path_)}
                    , {"st_mode", m}});
    }
    return type_;
}

void StatBase::copy_stat(StatBase &dst)
{
    dst.type_ = type_;
    dst.data_ = data_;
    dst.err_ = err_;
}

void StatBase::ensure_exists() const
{
    if (!exists()) error::raise({
            {"msg", "Logical error"}
            , {"reason", "File doesn't exist"}});
}

struct stat *StatBase::data()
{
    ensure_exists();
    return &data_;
}

struct stat const * StatBase::data() const
{
    ensure_exists();
    return &data_;
}

void copy_utime(int fd, Stat const &src)
{
    struct timespec times[2];
    ::memcpy(&times[0], &src.data()->st_atime, sizeof(times[0]));
    ::memcpy(&times[1], &src.data()->st_mtime, sizeof(times[1]));
    int rc = ::futimens(fd, times);
    if (rc < 0)
        error::raise({{"msg", "Can't change time"}
                , {"error", ::strerror(errno)}
                , {"target", fd}});
}

void copy_utime(std::string const &target, Stat const &src)
{
    struct timespec times[2];
    ::memcpy(&times[0], &src.data()->st_atime, sizeof(times[0]));
    ::memcpy(&times[1], &src.data()->st_mtime, sizeof(times[1]));
    int rc = ::utimensat(AT_FDCWD, target.c_str(), times, AT_SYMLINK_NOFOLLOW);
    if (rc < 0)
        error::raise({{"msg", "Can't change time"}
                , {"error", ::strerror(errno)}
                , {"target", qstr(target)}});
}

void mkdir(std::string const &path, mode_t mode)
{
    auto rc = ::mkdir(path.c_str(), mode);
    if (rc < 0)
        raise_std_error({{"msg", "Can't create dir"}
                , {"path", qstr(path)}});
}

Stat mkdir_similar(Stat const &from, Stat const &parent)
{
    if (!parent.exists())
        error::raise({{"msg", "No parent dir"}, {"parent", qstr(parent)}});

    auto dst_path = path(parent.path(), basename(from.path()));
    debug::debug("mkdir", dst_path);
    Stat dst_stat{dst_path};
    if (!dst_stat.exists()) {
        mkdir(dst_path, from.mode());
        dst_stat.refresh();
    } else if (dst_stat.file_type() == FileType::Dir) {
        debug::debug("Already exists", dst_path);
    } else {
        error::raise({{"msg", "Destination type is different"}
                , {"src", qstr(from)}, {"parent", qstr(parent)}
                , {"dst", qstr(dst_stat)}});
    }
    return std::move(dst_stat);
}

void unlink(std::string const &path)
{
    auto rc = ::unlink(path.c_str());
    if (rc < 0)
        error::raise({{"msg", "Can't unlink"}
                , {"path", qstr(path)}
                , {"error", ::strerror(errno)}});
}

void copy(cor::FdHandle &dst, cor::FdHandle &src, size_t left_size
                 , ErrorCallback on_error)
{
    int rc = ::ftruncate(dst.value(), left_size);
    if (rc < 0)
        on_error({{"msg", "Can't truncate"}});

    rc = ::lseek(dst.value(), left_size, SEEK_SET);
    if (rc < 0)
        on_error({{"msg", "Can't expand"}});

    static size_t max_chunk_size = 1024 * 1024;
    off_t off = 0;
    size_t size = left_size;
    while (left_size) {
        size = (left_size > max_chunk_size) ? max_chunk_size : left_size;
        auto p_src = mmap_create(nullptr, size, PROT_READ
                                 , MAP_PRIVATE, src.value(), off);
        auto p_dst = mmap_create(nullptr, size, PROT_READ | PROT_WRITE
                                 , MAP_SHARED, dst.value(), off);
        memcpy(mmap_ptr(p_dst), mmap_ptr(p_src), size);
        left_size -= size;
        off += size;
    }
}

cor::FdHandle copy_data(std::string const &dst_path
                        , Stat const &from, mode_t *pmode)
{
    cor::FdHandle src(::open(from.path().c_str(), O_RDONLY));
    if (!src.is_valid())
        error::raise({{"msg", "Cant' open src file"}
                , {"stat", qstr(from)}});
    auto raise_dst_error = [&dst_path](QVariantMap const &info) {
        error::raise(map({{"dst", qstr(dst_path)}
                    , {"error", ::strerror(errno)}}), info);
    };
    auto flags = O_RDWR | O_CREAT;
    cor::FdHandle dst(pmode
                      ? ::open(dst_path.c_str(), flags, *pmode)
                      : ::open(dst_path.c_str(), flags));
    if (!dst.is_valid())
        raise_dst_error({{"msg", "Cant' open dst file"}});
    using namespace std::placeholders;
    copy(dst, src, from.size(), std::bind(raise_dst_error, _1));
    return std::move(dst);
}


cor::FdHandle rewrite(std::string const &dst_path
                      , std::string const &text
                      , mode_t mode)
{
    auto flags = O_CREAT | O_TRUNC | O_WRONLY;
    cor::FdHandle dst(::open(dst_path.c_str(), flags, mode)
                      , cor::only_valid_handle);
    auto written = ::write(dst.value(), text.c_str(), text.size());
    if (written != (long)text.size())
        error::raise({{"msg", "Error writing"}
                , {"error", ::strerror(errno)}
                , {"path", qstr(dst_path)}
                , {"data", qstr(text)}
                , {"res", qstr(written)}});
    return std::move(dst);
}

std::string read_text(std::string const &src_path)
{
    std::ifstream src(src_path);
    std::string res{std::istreambuf_iterator<char>(src)
            , std::istreambuf_iterator<char>()};
    return res;
}

QString loggable(QDebug const&, FileType t)
{
    static const QString names[] = {
        "Socket", "Symlink", "File", "Block", "Dir", "Char"
        , "Fifo", "Absent", "Unknown"
    };
    static_assert(sizeof(names)/sizeof(names[0]) == cor::enum_size<FileType>() + 1
                  , "Check names size");
    return names[cor::enum_index(t)];
}

QDebug & operator << (QDebug &dst, Stat const &src)
{
    dst << QString::fromStdString(src.path())
        << "=(" << src.file_type() << ")";
    return dst;
}
