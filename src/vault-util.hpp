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

QDebug & operator << (QDebug &dst, Vault const &v)
{
    dst << "Vault[" << v.root() << "]";
    return dst;
}

QString Vault::find_root(QString const &path)
{
    subprocess::Process ps;
    QFileInfo info(path);
    ps.setWorkingDirectory(info.isDir() ? path : info.path());
    return ps.check_output("git-vault-root", {}).trimmed();
}

QString Vault::blobs() const
{
    return storage("blobs");
}

QString Vault::blob_path(QString const &hash) const
{
    if (hash.size() != SHA1_HASH_SIZE)
        error::raise({{"msg", "Wrong hash"}, {"hash", hash}});

    return path(blobs(), hash.mid(0, 2), hash.mid(2));
}


#endif // _VAULT_UTIL_HPP_
