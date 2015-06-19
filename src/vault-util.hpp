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
namespace git = qtaround::git;

#define SHA1_HASH_SIZE (40)

class Vault : public git::Tree
{
public:
    Vault(QString const &path)
        : git::Tree(find_root(path))
    {}

    QString root() const { return root_; }
    QString blobs() const;
    QString blob_path(QString const &) const;
private:
    QString find_root(QString const &path);
};

typedef std::shared_ptr<Vault> VaultHandle;

inline QDebug & operator << (QDebug &dst, Vault const &v)
{
    dst << "Vault[" << v.root() << "]";
    return dst;
}

inline QString Vault::blobs() const
{
    return storage("blobs");
}

#endif // _VAULT_UTIL_HPP_
