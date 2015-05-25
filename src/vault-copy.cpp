#include <qtaround/debug.hpp>
#include <qtaround/error.hpp>
#include <qtaround/os.hpp>
#include <qtaround/subprocess.hpp>
#include <cor/util.hpp>

#include <QCoreApplication>
#include <QCommandLineParser>

#include <deque>
#include <set>
#include <fstream>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace debug = qtaround::debug;
namespace os = qtaround::os;
namespace error = qtaround::error;
namespace subprocess = qtaround::subprocess;

template <typename T>
int loggable
(QDebug const& , T v, typename std::enable_if<std::is_enum<T>::value>::type* = 0)
{
    return static_cast<int>(v);
}

template <typename T>
QDebug & operator << (QDebug &dst, T v)
{
    dst << loggable(dst, v);
    return dst;
}

QCommandLineParser & operator <<
(QCommandLineParser &parser, QCommandLineOption const &opt)
{
    parser.addOption(opt);
    return parser;
}

enum class FileType : char {
    First_ = 0,
        Socket = First_, Symlink, File, Block, Dir, Char, Fifo,
        Absent, Last_ = Absent, Unknown
};

QString str(std::string const &s)
{
    return QString::fromStdString(s);
}

QString loggable(QDebug const&, std::string const &s)
{
    return str(s);
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

template <typename T>
bool is_valid(T v, typename std::enable_if<std::is_enum<T>::value>::type* = 0)
{
    auto i = static_cast<int>(v), first = static_cast<int>(T::First_)
        , last = static_cast<int>(T::Last_);
    return (v >= first && v <= last);
}

class StatBase {
public:
    StatBase(std::string const&);
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

QString str(Stat const &v)
{
    return str(v.path());
}

QString loggable(QDebug const&, Stat const &v)
{
    return str(v);
}

QString loggable(QDebug const&, FileId const &s)
{
    return QString("(Node: %1 %2)").arg(s.st_dev).arg(s.st_ino);
}

template <typename T1, typename T2>
QString loggable(QDebug const &d, std::pair<T1, T2> const &s)
{
    return QString("(%1 %2)").arg(loggable(d, s.first)).arg(loggable(d, s.second));
}

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

Stat readlink(Stat const &from)
{
    auto resolved = readlink(from.path());
    return Stat(resolved);
}

std::string basename(std::string const &path)
{
    auto res = ::basename(path.c_str());
    return res ? res : "";
}

std::string path(std::initializer_list<std::string> parts)
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

template <typename T>
T log_result(std::string const &name, T &&res)
{
    debug::debug(name, res);
    return std::move(res);
}

bool operator == (FileId const &a, FileId const &b)
{
    return (a.st_dev == b.st_dev) && (a.st_ino == b.st_ino);
}

bool operator < (FileId const &a, FileId const &b)
{
    return (a.st_dev < b.st_dev) || (a.st_ino < b.st_ino);
}

bool operator == (Stat const &a, Stat const &b)
{
    return a.exists() && b.exists() && a.id() == b.id();
}

QDebug & operator << (QDebug &dst, Stat const &src)
{
    dst << QString::fromStdString(src.path())
        << "=(" << src.file_type() << ")";
    return dst;
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

class Dir {
public:
    Dir(std::string const &name)
        : dir_(opendir(name.c_str()))
        , entry_(nullptr)
    {}

    bool next()
    {
        bool res = false;
        if (dir_) {
            entry_ = readdir(dir_);
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


static void copy_utime(int fd, Stat const &src)
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

static void copy_utime(std::string const &target, Stat const &src)
{
    struct timespec times[2];
    ::memcpy(&times[0], &src.data()->st_atime, sizeof(times[0]));
    ::memcpy(&times[1], &src.data()->st_mtime, sizeof(times[1]));
    int rc = ::utimensat(AT_FDCWD, target.c_str(), times, AT_SYMLINK_NOFOLLOW);
    if (rc < 0)
        error::raise({{"msg", "Can't change time"}
                , {"error", ::strerror(errno)}
                , {"target", str(target)}});
}

static Stat mkdir_similar(Stat const &from, Stat const &parent)
{
    if (!parent.exists())
        error::raise({{"msg", "No parent dir"}, {"parent", str(parent)}});

    auto dst_path = path(parent.path(), basename(from.path()));
    debug::debug("mkdir", dst_path);
    Stat dst_stat{dst_path};
    if (!dst_stat.exists()) {
        int rc = ::mkdir(dst_path.c_str(), from.mode());
        if (rc < 0)
            error::raise({{"msg", "Can't create dir"}
                    , {"path", str(dst_path)}
                    , {"err", ::strerror(errno)}});
        dst_stat.refresh();
    } else if (dst_stat.file_type() == FileType::Dir) {
        debug::debug("Already exists", dst_path);
    } else {
        error::raise({{"msg", "Destination type is different"}
                , {"src", str(from)}, {"parent", str(parent)}
                , {"dst", str(dst_stat)}});
    }
    return std::move(dst_stat);
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
static MMapHandle mmap_create
(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    MMapHandle res(MMap(::mmap(addr, length, prot, flags, fd, offset), length)
                   , cor::only_valid_handle);
    return std::move(res);
}

void *mmap_ptr(MMapHandle const &p)
{
    return p.cref().p;
}

static void unlink(std::string const &path)
{
    auto rc = ::unlink(path.c_str());
    if (rc < 0)
        error::raise({{"msg", "Can't unlink"}
                , {"path", str(path)}
                , {"error", ::strerror(errno)}});
}

static void copy
(cor::FdHandle &dst, cor::FdHandle &src, size_t left_size
 , std::function<void (QVariantMap const&)> on_error)
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

static cor::FdHandle copy_data(std::string const &dst_path
                               , Stat const &from, mode_t *pmode)
{
    cor::FdHandle src(::open(from.path().c_str(), O_RDONLY));
    if (!src.is_valid())
        error::raise({{"msg", "Cant' open src file"}
                , {"stat", str(from)}});
    auto raise_dst_error = [&dst_path](QVariantMap const &info) {
        error::raise(map({{"dst", str(dst_path)}
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


static cor::FdHandle rewrite(std::string const &dst_path
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
                , {"path", str(dst_path)}
                , {"data", str(text)}
                , {"res", str(written)}});
    return std::move(dst);
}

std::string read_text(std::string const &src_path)
{
    std::ifstream src(src_path);
    std::string res{std::istreambuf_iterator<char>(src)
            , std::istreambuf_iterator<char>()};
    return res;
}

#define SHA1_HASH_SIZE (40)

class Vault
{
public:
    Vault(QString const &path)
        : root_(find_root(path))
        , storage_()
    {}

    std::string root() const { return root_; }
    std::string storage() const;
    std::string blobs() const;
    std::string blob_path(std::string const &) const;
private:

    static std::string find_root(QString const &);
    static std::string storage_resolve(std::string const &);
    std::string root_;
    mutable std::string storage_;
};

typedef std::shared_ptr<Vault> vault_handle;

template <typename T>
QDebug & operator << (QDebug &dst, std::shared_ptr<T> const &p)
{
    dst << "ptr=(";
    if (p) dst << *p; else dst << "null";
    dst << ")";
    return dst;
}

QDebug & operator << (QDebug &dst, Vault const &v)
{
    dst << "Vault[" << str(v.root()) << "]";
    return dst;
}

std::string Vault::find_root(QString const &path)
{
    subprocess::Process ps;
    ps.setWorkingDirectory(path);
    return str(ps.check_output("git-vault-root", {})).toStdString();
}

std::string Vault::storage() const
{
    if (storage_.empty())
        storage_ = storage_resolve(root_);
    return storage_;
}

std::string Vault::blobs() const
{
    return path(storage(), "blobs");
}

std::string Vault::blob_path(std::string const &hash) const
{
    if (hash.size() != SHA1_HASH_SIZE)
        error::raise({{"msg", "Wrong hash"}, {"hash", str(hash)}});

    return path(blobs(), hash.substr(0, 2), hash.substr(2));
}


std::string Vault::storage_resolve(std::string const &root)
{
    std::string res;
    Stat dotgit(path(path(root, ".git")));
    if (dotgit.file_type() == FileType::Dir) {
        res = path(dotgit.path(), "blobs");
    } else if (dotgit.file_type() == FileType::File) {
        auto data = read_text(dotgit.path());
        static const std::string prefix{"gitdir: "};
        if (data.substr(0, prefix.size()) != prefix)
            error::raise({{"msg", "Wrong .git data"}, {"data", str(data)}});
        res = data.substr(prefix.size(), data.find_first_of(" "));
    }
    return path_normalize(res);
}

static std::string git_hash_file(std::string const &path)
{
    return str(subprocess::check_output
               ("git", {"hash-object", str(path)})).trimmed()
        .toStdString();
}

enum class Action { Import = 0, Export };


static cor::FdHandle copy_blob(Action action
                               , std::string const &dst_path
                               , Stat const &from
                               , vault_handle const &root)
{
    if (action == Action::Export) {
        auto data_hash = git_hash_file(from.path());
        auto data_dst_path = root->blob_path(data_hash);
        Stat data_stat(data_dst_path);
        if (!data_stat.exists()) {
            copy_data(data_dst_path, from, nullptr);
        }
        return rewrite(dst_path, data_hash, from.mode());
    } else {
        auto data_hash = read_text(from.path());
        Stat data_stat(root->blob_path(data_hash));
        auto mode = from.mode();
        return copy_data(dst_path, data_stat, &mode);
    }
}

enum class Options { Vault, Blobs, Recursive, NoClobber, Deref, Last_ = Deref };
typedef Record<Options
               , vault_handle, bool, bool, bool, bool> options_type;
template <> struct RecordTraits<options_type> {
    RECORD_FIELD_NAMES(options_type
                       , "Vault", "Blobs", "Recursive", "NoClobber", "Deref");
};


static Stat file_copy(Stat const &from, Stat const &parent
                      , options_type const &options
                      , Action action)
{
    debug::debug("Copy file", from, parent);
    auto dst_path = path(parent.path(), basename(from.path()));
    Stat dst_stat{dst_path};
    if (dst_stat.exists()) {
        if (options.get<Options::NoClobber>()) {
            debug::debug("Do not overwrite", dst_stat.path());
            return std::move(dst_stat);
        }
        switch (dst_stat.file_type()) {
        case FileType::File:
            break;
        case FileType::Symlink:
            unlink(dst_stat.path());
        default:
            return std::move(dst_stat);
            break;
        }
    }
    auto mode = from.mode();
    auto dst = (options.get<Options::Blobs>()
                ? copy_blob(action, dst_path, from
                            , options.get<Options::Vault>())
                : copy_data(dst_path, from, &mode));
    copy_utime(dst.value(), from);
    dst.close();
    dst_stat.refresh();
    return std::move(dst_stat);
}

enum class Context { Options, Action, SrcStat, DstStat, Last_ = DstStat };

typedef Record<Context
               , options_type, Action, Stat, Stat> context_type;
template <> struct RecordTraits<context_type> {
    RECORD_FIELD_NAMES(context_type
                       , "Options", "Action", "SrcStat" , "DstStat");
};

Action actionFromName(QString const &name)
{
    Action res;
    if (name == "import")
        res = Action::Import;
    else if (name == "export")
        res = Action::Export;
    else
        error::raise({{"msg", "Parameter 'action' is unknown"}
                , {"action", name}});
    return res;
}

class Processor {
public:
    enum class End { Front, Back };
    typedef std::shared_ptr<context_type> data_ptr;
    void add(data_ptr const &, End end = End::Back);
    void execute();
private:

    void onDir(data_ptr const &);
    std::deque<data_ptr> todo_;
    std::set<std::pair<FileId, FileId> > visited_;
};

void Processor::add(data_ptr const &p, End end)
{
    debug::debug("Adding", *p);
    auto push = [this, end](data_ptr const &p) {
        if (end == End::Back)
            todo_.push_back(p);
        else
            todo_.push_front(p);
    };
    auto const &opts = p->get<Context::Options>();
    auto const &src_stat = p->get<Context::SrcStat>();
    if (src_stat.file_type() == FileType::Dir) {
        if (opts.get<Options::Recursive>())
            push(p);
        else
            debug::info("Omiting directory", src_stat.path());
    } else {
        push(p);
    }
}

void Processor::execute()
{
    auto onFile = [this](context_type const &ctx) {
        auto const &src_stat = ctx.get<Context::SrcStat>();
        auto const &tgt_stat = ctx.get<Context::DstStat>();
        auto dst_stat = file_copy
        (src_stat, tgt_stat
         , ctx.get<Context::Options>()
         , ctx.get<Context::Action>());
    };
    auto onSymlink = [this](context_type const &ctx) {
        auto const &src_stat = ctx.get<Context::SrcStat>();
        //auto const &dst_stat = ctx.get<Context::DstStat>();
        auto const &opts = ctx.get<Context::Options>();
        if (opts.get<Options::Deref>()) {
            auto new_path = readlink(src_stat.path());
            auto new_ctx = std::make_shared<context_type>(ctx);
            new_ctx->get<Context::SrcStat>() = Stat(new_path);
            this->add(new_ctx, End::Front);
        } else {
            // TODO
        }
    };
    auto onDir = [this](context_type const &ctx) {
        auto const &options = ctx.get<Context::Options>();
        auto const &src = ctx.get<Context::SrcStat>();
        auto const &target = ctx.get<Context::DstStat>();
        auto dst = mkdir_similar(src, target);
        if (!options.get<Options::NoClobber>())
            copy_utime(dst.path(), src);

        Dir d{src.path()};
        while (d.next()) {
            auto name = d.name();
            if (name == "." || name == "..")
                continue;

            debug::debug("Entry", d.name());
            auto item = std::make_shared<context_type>
                (options, ctx.get<Context::Action>()
                 , Stat(path(src, d.name())), dst);
            this->add(item, End::Front);
        }
    };
    auto operationId = [](context_type const &ctx) {
        return std::make_pair(ctx.get<Context::SrcStat>().id()
                              , ctx.get<Context::DstStat>().id());
    };
    auto markVisited = [operationId, this](context_type const &ctx) {
        visited_.insert(operationId(ctx));
    };
    auto isVisited = [operationId, this](context_type const &ctx) {
        return (visited_.find(operationId(ctx)) != visited_.end());
    };

    while (!todo_.empty()) {
        auto item = todo_.front();
        todo_.pop_front();
        auto const &src_stat = item->get<Context::SrcStat>();
        auto &dst_stat = item->get<Context::DstStat>();
        debug::debug("Processing", src_stat.path());
        dst_stat.refresh();
        if (!isVisited(*item)) {
            switch (src_stat.file_type()) {
            case FileType::Symlink:
                onSymlink(*item);
                break;
            case FileType::Dir:
                onDir(*item);
                break;
            case FileType::File:
                onFile(*item);
                break;
            default:
                debug::debug("No handler for", src_stat.file_type());
                break;
            }
            markVisited(*item);
        } else {
            debug::info("Skip duplicate", *item);
        }
    }
}

int main_(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("vault-copy");
    parser.addHelpOption();

    parser << QCommandLineOption({"a", "action"},
                                 "The action passed by vault", "ACTION")
         << QCommandLineOption({"L", "dereference"},
                                 "Follow symlinks in src")
         << QCommandLineOption({"n", "no-clobber"},
                                 "Do not overwrite an existing file")
         << QCommandLineOption({"b", "blobs"},
                                 "Use blob mode")
         << QCommandLineOption({"r", "recursive"},
                                 "Copy directories recursively")
        ;
    parser.addPositionalArgument("src", "Source file/directory");
    parser.addPositionalArgument("dst", "Destination file/directory");
    parser.process(app);

    auto args = parser.positionalArguments();
    auto last = args.size() - 1;
    if (last < 2)
        error::raise({{"msg", "There is no src or dst"}});
    auto dst = args[last];
    auto action = actionFromName(parser.value("action"));
    auto vault = std::make_shared<Vault>(action == Action::Import? args[0] : dst);
    options_type options{vault
            , parser.isSet("recursive")
            , parser.isSet("blobs")
            , parser.isSet("no-clobber")
            , parser.isSet("dereference")
            };

    Processor processor;
    for (auto i = 0; i < last; ++i) {
        auto src = args[i];
        auto ctx = std::make_shared<context_type>
            (options, action
             , Stat(src.toStdString())
             , Stat(dst.toStdString()));
        processor.add(ctx);
    }
    processor.execute();
    return 0;
}

int main(int argc, char **argv)
{
    int res = 1;
    try {
        res = main_(argc, argv);
    } catch (std::exception const &e) {
        debug::error("Error:",  e.what());
    } catch (...) {
        debug::error("Unknown error");
    }
    return res;
}
