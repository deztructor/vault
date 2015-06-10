#ifndef _COR_GIT_UTIL_HPP_
#define _COR_GIT_UTIL_HPP_

#include "common-util.hpp"
#include "file-util.hpp"
#include <cor/util.hpp>
#include <qtaround/subprocess.hpp>
#include <qtaround/util.hpp>

namespace cor {

template <typename ... Args>
std::string join(std::string const &delim, Args&& ...args)
{
    return cor::join(std::vector<std::string>{args...}, delim);
}

namespace git {

class Tree {
public:
    Tree(std::string const &path);
    std::string storage() const;

    template <typename ... Args>
    std::string storage(std::string const &arg1, Args&& ...args) const
    {
        return path(storage(), arg1, std::forward<Args>(args)...);
    }

    std::string execute(QStringList const &params);

    template <typename ... Args>
    std::string execute(QString const &arg1, Args&& ...args)
    {
        return execute({arg1, std::forward<Args>(args)...});
    }

    std::string hash_file(std::string const &path)
    {
        return execute("hash-object", qstr(path));
    }

    std::string blob_add(std::string const &path)
    {
        return execute("hash-object", "-w", "-t", "blob", qstr(path));
    }

    std::string index_add(std::string const &hash
                                 , std::string const &name
                                 , mode_t mode = 0100644)
    {
        auto cacheinfo = join(",", std::to_string(mode), hash, name);
        return execute("update-index", "--add", "--cacheinfo", qstr(cacheinfo));
    }

    size_t blob_add(int src, size_t left_size, size_t max_chunk_size
                    , std::string const &entry_name);
    
private:
    static std::string resolve_storage(std::string const &root);

    std::string root_;
    qtaround::subprocess::Process ps_;
    mutable std::string storage_;
};


}
}

#endif // _COR_GIT_UTIL_HPP_
