#ifndef _VAULT_UTIL_HPP_
#define _VAULT_UTIL_HPP_

#include "git-util.hpp"
#include "common-util.hpp"

#define SHA1_HASH_SIZE (40)

class Vault : public qtaround::git::Tree
{
public:
    Vault(QString const &path)
        : qtaround::git::Tree(find_root(path))
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
