#include "file-util.hpp"
#include "vault-util.hpp"
#include <qtaround/error.hpp>
#include <qtaround/os.hpp>
#include <QCoreApplication>
#include <set>

namespace error = qtaround::error;
namespace os = qtaround::os;

QDebug & operator << (QDebug &dst, QFileInfo const &v)
{
    dst << str(v);
    return dst;
}

enum class Action { Import = 0, Export };


static FileHandle copy_blob(Action action
                            , QString const &dst_path
                            , QFileInfo const &from
                            , VaultHandle const &root)
{
    if (action == Action::Export) {
        auto data_hash = root->hash_file(from.filePath());
        auto data_dst_path = root->blob_path(data_hash);
        QFileInfo data_stat(data_dst_path);
        if (!data_stat.exists()) {
            auto blob_dir = os::path::dirName(data_stat);
            mkdir(blob_dir, permissions::dirForUserOnly);
            copy_data(data_dst_path, from, nullptr);
        }
        return rewrite(dst_path, data_hash, from.permissions());
    } else {
        auto data_hash = read_text(from.filePath());
        QFileInfo data_stat(root->blob_path(data_hash));
        auto mode = from.permissions();
        return copy_data(dst_path, data_stat, &mode);
    }
}

enum class Depth { Shallow = 0, Recursive };
enum class Overwrite { No = 0, Yes };
enum class Deref { No = 0, Yes };
enum class DataHint { Compact = 0, Big };
enum class Options { Vault, Data, Depth, Overwrite, Deref, Last_ = Deref };
typedef Record<Options
               , VaultHandle, DataHint, Depth, Overwrite, Deref> options_type;
template <> struct RecordTraits<options_type> {
    RECORD_FIELD_NAMES(options_type
                       , "Vault", "Data", "Depth", "Overwrite", "Deref");
};


static QFileInfo file_copy(QFileInfo const &from, QFileInfo const &parent
                           , options_type const &options
                           , Action action)
{
    debug::debug("Copy file", from, parent);
    auto dst_path = path(parent.filePath(), os::path::baseName(from.filePath()));
    QFileInfo dst_stat(dst_path);
    if (dst_stat.exists()) {
        if (options.get<Options::Overwrite>() == Overwrite::No) {
            debug::debug("Do not overwrite", dst_stat.filePath());
            return std::move(dst_stat);
        }
        if (!dst_stat.isFile()) {
            if (dst_stat.isSymLink())
                unlink(dst_stat.filePath());
            else
                return std::move(dst_stat);
        }
    }
    auto mode = from.permissions();
    auto dst = (options.get<Options::Data>() == DataHint::Big
                ? copy_blob(action, dst_path, from
                            , options.get<Options::Vault>())
                : copy_data(dst_path, from, &mode));
    copy_utime(*dst, from);
    dst->close();
    dst_stat.refresh();
    return std::move(dst_stat);
}

enum class Context { Options, Action, Src, Dst, Last_ = Dst };

typedef Record<Context
               , options_type, Action, QFileInfo, QFileInfo> context_type;
template <> struct RecordTraits<context_type> {
    RECORD_FIELD_NAMES(context_type
                       , "Options", "Action", "Src" , "Dst");
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
    auto const &src_stat = p->get<Context::Src>();
    if (src_stat.isDir()) {
        if (opts.get<Options::Depth>() == Depth::Recursive)
            push(p);
        else
            debug::info("Omiting directory", src_stat.filePath());
    } else {
        push(p);
    }
}

void Processor::execute()
{
    auto onFile = [this](context_type const &ctx) {
        auto const &src_stat = ctx.get<Context::Src>();
        auto const &tgt_stat = ctx.get<Context::Dst>();
        debug::debug("File: ", src_stat);
        auto dst_stat = file_copy
        (src_stat, tgt_stat
         , ctx.get<Context::Options>()
         , ctx.get<Context::Action>());
    };
    auto onSymlink = [this](context_type const &ctx) {
        auto const &src = ctx.get<Context::Src>();
        debug::debug("Symlink: ", src);
        auto const &dst = ctx.get<Context::Dst>();
        auto const &opts = ctx.get<Context::Options>();
        if (opts.get<Options::Deref>() == Deref::Yes) {
            auto new_path = readlink(src.filePath());
            auto new_ctx = std::make_shared<context_type>(ctx);
            new_ctx->get<Context::Src>() = QFileInfo(new_path);
            this->add(new_ctx, End::Front);
        } else {
            symlink(readlink(src).filePath(), path(dst, src.fileName()));
        }
    };
    auto onDir = [this](context_type const &ctx) {
        auto const &options = ctx.get<Context::Options>();
        auto const &src = ctx.get<Context::Src>();
        debug::debug("Dir: ", src);
        auto const &target = ctx.get<Context::Dst>();
        auto dst = mkdir_similar(src, target);
        if (options.get<Options::Overwrite>() == Overwrite::Yes)
            copy_utime(dst.filePath(), src);

        auto entries = QDir(src.filePath()).entryInfoList(QDir::NoDotAndDotDot);
        while (!entries.isEmpty()) {
            auto entry = entries.takeFirst();

            debug::debug("Entry", entry.baseName());
            auto item = std::make_shared<context_type>
                (options, ctx.get<Context::Action>()
                 , QFileInfo(entry.filePath()), dst);
            this->add(item, End::Front);
        }
    };
    auto operationId = [](context_type const &ctx) {
        return std::make_pair(fileId(ctx.get<Context::Src>())
                              , fileId(ctx.get<Context::Dst>()));
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
        auto const &src = item->get<Context::Src>();
        auto &dst = item->get<Context::Dst>();
        debug::debug("Processing", src.filePath());
        dst.refresh();
        if (!isVisited(*item)) {
            if (src.isSymLink()) {
                onSymlink(*item);
            } else if (src.isDir()) {
                onDir(*item);
            } else if (src.isFile()) {
                onFile(*item);
            } else {
                debug::debug("No handler for the type", src.filePath());
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
    if (last < 1)
        error::raise({{"msg", "There is no src or dst"}, {"args", args}});
    auto dst = args[last];
    auto action = actionFromName(parser.value("action"));
    auto vault = std::make_shared<Vault>(action == Action::Import? args[0] : dst);
    options_type options{vault
            , parser.isSet("blobs") ? DataHint::Big : DataHint::Compact
            , parser.isSet("recursive") ? Depth::Recursive : Depth::Shallow
            , parser.isSet("no-clobber") ? Overwrite::No : Overwrite::Yes
            , parser.isSet("dereference") ? Deref::Yes : Deref::No
            };

    Processor processor;
    for (auto i = 0; i < last; ++i) {
        auto src = args[i];
        debug::debug("Source", src);
        auto ctx = std::make_shared<context_type>
            (options, action, QFileInfo(src), QFileInfo(dst));
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
