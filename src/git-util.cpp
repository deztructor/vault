#include "git-util.hpp"
#include "file-util.hpp"
#include "common-util.hpp"
#include <qtaround/subprocess.hpp>
#include <qtaround/error.hpp>
#include <qtaround/util.hpp>
#include <QVariantMap>
#include <stdlib.h>

namespace subprocess = qtaround::subprocess;
namespace error = qtaround::error;

namespace cor { namespace git {

Tree::Tree(std::string const &path)
    : root_(path)
{
    ps_.setWorkingDirectory(qstr(root_));
}

std::string Tree::execute(QStringList const &params)
{
    auto out = qstr(ps_.check_output("git", {params}));
    return out.trimmed().toStdString();
}

std::string Tree::resolve_storage(std::string const &root)
{
    std::string res;
    Stat dotgit(path(root, ".git"));
    debug::debug("dotgit type for", path(root, ".git"), "=", dotgit, " is ", dotgit.file_type());
    switch (dotgit.file_type()) {
    case FileType::Dir:
        res = dotgit.path();
        break;
    case FileType::File: {
        auto data = read_text(dotgit.path());
        static const std::string prefix{"gitdir: "};
        if (data.substr(0, prefix.size()) != prefix)
            error::raise({{"msg", "Wrong .git data"}, {"data", qstr(data)}});
        res = data.substr(prefix.size(), data.find_first_of(" "));
        break;
    }
    default:
        throw cor::Error("Unhandled .git type", dotgit.file_type());
        break;
    }
    return path_normalize(res);
}

std::string Tree::storage() const
{
    if (storage_.empty())
        storage_ = resolve_storage(root_);
    return storage_;
}

size_t Tree::blob_add(int src, size_t left_size, size_t max_chunk_size
                      , std::string const &entry_name)
{
    auto copy = [](int src, int dst, size_t size, off_t off) {
        auto p_src = mmap_create(nullptr, size, PROT_READ
                                 , MAP_PRIVATE, src, off);
        auto p_dst = mmap_create(nullptr, size, PROT_READ | PROT_WRITE
                                 , MAP_SHARED, dst, off);
        memcpy(mmap_ptr(p_dst), mmap_ptr(p_src), size);
    };
    size_t idx = 0;
    off_t off = 0;
    size_t size = left_size;
    CFileHandle dst(::tmpfile());
    auto dst_path = get_fname(::fileno(dst.value()));
    if (!dst.is_valid())
        raise_std_error({{"msg", "Can't open tmp file"}, {"path", qstr(dst_path)}});

    if (left_size > max_chunk_size) {
        while (left_size) {
            size = (left_size > max_chunk_size) ? max_chunk_size : left_size;
            copy(src, ::fileno(dst.value()), size, off);
            left_size -= size;
            off += size;
            auto dst_hash = blob_add(dst_path);
            index_add(dst_hash, path(entry_name, std::to_string(idx++)));
        }
    } else {
        copy(src, ::fileno(dst.value()), size, off);
        auto dst_hash = blob_add(dst_path);
        index_add(dst_hash, entry_name);
    }
    unlink(dst_path);
    return idx;
}

}}
