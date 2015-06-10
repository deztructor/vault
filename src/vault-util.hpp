#ifndef _VAULT_UTIL_HPP_
#define _VAULT_UTIL_HPP_

// #include <qtaround/os.hpp>
#include <qtaround/subprocess.hpp>
// #include <cor/util.hpp>

#include "git-util.hpp"
#include "common-util.hpp"

// #include <QCoreApplication>
// #include <QCommandLineParser>

// #include <deque>
// #include <set>

// #include <sys/types.h>
// #include <sys/stat.h>
// #include <unistd.h>
// #include <fcntl.h>

namespace error = qtaround::error;
namespace subprocess = qtaround::subprocess;
namespace git = cor::git;

#define SHA1_HASH_SIZE (40)

class Vault : public git::Tree
{
public:
    Vault(QString const &path)
        : git::Tree(find_root(path))
    {}

    std::string root() const { return root_; }
    std::string blobs() const;
    std::string blob_path(std::string const &) const;
private:

    std::string find_root(QString const &path);
    std::string root_;
    mutable std::string storage_;
};

typedef std::shared_ptr<Vault> VaultHandle;

QDebug & operator << (QDebug &dst, Vault const &v)
{
    dst << "Vault[" << qstr(v.root()) << "]";
    return dst;
}

std::string Vault::find_root(QString const &path)
{
    subprocess::Process ps;
    ps.setWorkingDirectory(path);
    auto res = qstr(ps.check_output("git-vault-root", {})).trimmed();
    return res.toStdString();
}

std::string Vault::blobs() const
{
    return storage("blobs");
}

std::string Vault::blob_path(std::string const &hash) const
{
    if (hash.size() != SHA1_HASH_SIZE)
        error::raise({{"msg", "Wrong hash"}, {"hash", qstr(hash)}});

    return path(blobs(), hash.substr(0, 2), hash.substr(2));
}


#endif // _VAULT_UTIL_HPP_
