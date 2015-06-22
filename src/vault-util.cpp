#include "config.hpp"
#include "vault-util.hpp"

#include <qtaround/subprocess.hpp>

namespace error = qtaround::error;
namespace subprocess = qtaround::subprocess;

QString Vault::find_root(QString const &path)
{
    subprocess::Process ps;
    QFileInfo info(path);
    auto wd = info.isDir() ? path : info.path();
    debug::debug("find root for", wd);
    ps.setWorkingDirectory(wd);
    return ps.check_output(VAULT_LIBEXEC_PATH "/git-vault-root", {}).trimmed();
}

QString Vault::blob_path(QString const &hash) const
{
    if (hash.size() != SHA1_HASH_SIZE)
        error::raise({{"msg", "Wrong hash"}, {"hash", hash}});

    return path(blobs(), hash.mid(0, 2), hash.mid(2));
}
