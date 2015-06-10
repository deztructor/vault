#ifndef _FILE_UTIL_HPP_
#define _FILE_UTIL_HPP_

#include "common-util.hpp"
#include <cor/util.hpp>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

enum class FileType : char {
    First_ = 0,
        Socket = First_, Symlink, File, Block, Dir, Char, Fifo,
        Absent, Last_ = Absent, Unknown
};

class StatBase {
public:
    explicit StatBase(std::string const&);
    StatBase(StatBase &&) = default;
    StatBase(StatBase const &) = default;
    StatBase& operator = (StatBase &&) = default;
    StatBase& operator = (StatBase const&) = default;

    bool exists() const { return type_ != FileType:: Absent; }
    struct stat *data();
    struct stat const * data() const;
    void refresh();
    void copy_stat(StatBase &);
    FileType file_type() const;

    std::string path_;
private:
    void ensure_exists() const;

    mutable FileType type_;
    int err_;
    struct stat data_;
};

struct FileId
{
    FileId(struct stat const &src)
        : st_dev(src.st_dev), st_ino(src.st_ino)
    {}
    dev_t st_dev;
    ino_t st_ino;
};

class Stat : private StatBase {
public:
    Stat(std::string const &path) : StatBase(path) {}
    Stat(Stat &&) = default;
    Stat(Stat const&) = default;
    Stat& operator = (Stat &&) = default;
    Stat& operator = (Stat const &) = default;

    void copy_stat(Stat &dst) {
        StatBase::copy_stat(static_cast<StatBase &>(dst));
    }
    FileType file_type() const { return StatBase::file_type(); }
    bool exists() const { return StatBase::exists(); }
    void refresh() { StatBase::refresh(); }
    std::string path() const { return path_; }
    struct stat const * data() const { return StatBase::data(); }
    FileId id() const { return FileId(*data()); }
    off_t size() const { return data()->st_size; }
    mode_t mode() const { return data()->st_mode; }
};


class Dir {
public:
    Dir(std::string const &name)
        : dir_(::opendir(name.c_str()))
        , entry_(nullptr)
    {}

    bool next()
    {
        bool res = false;
        if (dir_) {
            entry_ = ::readdir(dir_);
            res = (entry_ != nullptr);
        }
        return res;
    }

    std::string name() const
    {
        return entry_ ? entry_->d_name : "";
    }

    ~Dir() {
        if (dir_) ::closedir(dir_);
    }
private:
    DIR *dir_;
    struct dirent *entry_;
};

std::string path_normalize(std::string const &);
std::string readlink(std::string const &);

namespace {

inline Stat readlink(Stat const &from)
{
    auto resolved = readlink(from.path());
    return Stat(resolved);
}

inline std::string basename(std::string const &path)
{
    auto str = strdup_unique(path.c_str());
    auto res = ::basename(str.get());
    return res ? res : "";
}

inline std::string dirname(std::string const &path)
{
    auto str = strdup_unique(path.c_str());
    auto res = ::dirname(str.get());
    return res ? res : "";
}

inline std::string path(std::initializer_list<std::string> parts)
{
    return cor::join(std::move(parts), "/");
}

template <typename ... Args>
std::string path(std::string v, Args&& ...args)
{
    return path({v, args...});
}

template <typename ... Args>
std::string path(Stat const &root, Args&& ...args)
{
    return path({root.path(), args...});
}

}

inline bool operator == (FileId const &a, FileId const &b)
{
    return (a.st_dev == b.st_dev) && (a.st_ino == b.st_ino);
}

inline bool operator < (FileId const &a, FileId const &b)
{
    return (a.st_dev < b.st_dev) || (a.st_ino < b.st_ino);
}

inline bool operator == (Stat const &a, Stat const &b)
{
    return a.exists() && b.exists() && a.id() == b.id();
}


struct MMap {
    MMap(void *pp, size_t l) : p(pp), len(l) { }
    void *p;
    size_t len;
};

struct MMapTraits
{
    typedef MMap handle_type;
    void close_(handle_type v) { ::munmap(v.p, v.len); }
    bool is_valid_(handle_type v) const { return v.p != nullptr; }
    handle_type invalid_() const { return MMap(nullptr, 0); }
};

typedef cor::Handle<MMapTraits> MMapHandle;
namespace {

inline MMapHandle mmap_create
(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    MMapHandle res(MMap(::mmap(addr, length, prot, flags, fd, offset), length)
                   , cor::only_valid_handle);
    return std::move(res);
}

inline void *mmap_ptr(MMapHandle const &p)
{
    return p.cref().p;
}

}

void copy_utime(int target_fd, Stat const &src);
void copy_utime(std::string const &target, Stat const &src);
Stat mkdir_similar(Stat const &from, Stat const &parent);
void unlink(std::string const &path);
void copy(cor::FdHandle &dst, cor::FdHandle &src, size_t left_size
          , ErrorCallback on_error);
cor::FdHandle copy_data(std::string const &dst_path
                        , Stat const &from, mode_t *pmode);
cor::FdHandle rewrite(std::string const &dst_path
                      , std::string const &text
                      , mode_t mode);
std::string read_text(std::string const &src_path);
void mkdir(std::string const &path, mode_t mode);

// -----------------------------------------------------------------------------

inline QString qstr(Stat const &v)
{
    return qstr(v.path());
}

inline QString loggable(QDebug const&, Stat const &v)
{
    return qstr(v);
}

inline QString loggable(QDebug const&, FileId const &s)
{
    return QString("(Node: %1 %2)").arg(s.st_dev).arg(s.st_ino);
}

template <typename T1, typename T2>
QString loggable(QDebug const &d, std::pair<T1, T2> const &s)
{
    return QString("(%1 %2)").arg(loggable(d, s.first)).arg(loggable(d, s.second));
}

QString loggable(QDebug const&, FileType t);

QDebug & operator << (QDebug &dst, Stat const &src);

inline std::string get_fname(int fd)
{
    auto fd_path = path("/proc/self/fd", std::to_string(fd));
    return read_text(fd_path);
}

struct CFileTraits
{
    typedef FILE * handle_type;
    void close_(handle_type v) { ::fclose(v); }
    bool is_valid_(handle_type v) const { return v != nullptr; }
    FILE* invalid_() const { return nullptr; }
};

typedef cor::Handle<CFileTraits> CFileHandle;

#endif // _FILE_UTIL_HPP_
