
#include <QDir>

#include <libgit/repo.h>
#include <libgit/repostatus.h>
#include <libgit/commit.h>

#include "vault_config.hpp"
#include "error.hpp"
#include "os.hpp"
#include "debug.hpp"
#include "json.hpp"

namespace vault { namespace config {

Unit::Unit()
{

}

Unit::Unit(const QVariantMap &data)
    : m_data(data)
{
    m_data["is_unit_config"] = true;
}

Unit &Unit::read(const QString &fname)
{
    update(json::read(fname).toVariantMap());
    return *this;
}

ssize_t Unit::write(const QString &fname)
{
    return json::write(m_data, fname);
}

bool Unit::update(const QVariantMap &data)
{
    QVariantMap src = data;
    bool updated = false;
    if (!src.value("is_unit_config").toBool()) {
        if (!src.contains("name") || !src.contains("script")) {
            error::raise({{"msg", "Unit description should contain name and script"}});
        }
        src["script"] = os::path::canonical(src.value("script").toString());
    }
    for (auto i = src.begin(); i != src.end(); ++i) {
        if (!m_data.contains(i.key()) || i.value() != m_data.value(i.key())) {
            m_data[i.key()] = i.value();
            updated = true;
        }
    }
    return updated;
}

QString Unit::name() const
{
    return m_data.value("name").toString();
}

QString Unit::script() const
{
    return m_data.value("script").toString();
}


static const char *moduleExt = ".json";

Config::Config(const QString &unitsDir)
      : m_unitsDir(unitsDir)
{
    if (unitsDir.isEmpty()) {
        error::raise({{"msg", "Wrong configuration"}, {"cfg", unitsDir}});
    }

    load();
}

Config::~Config()
{
}

void Config::load()
{
    if (!os::path::exists(m_unitsDir)) {
        return;
    }

    QDir d(m_unitsDir);
    for (const QString &fname: d.entryList({ QLatin1String("*") + moduleExt })) {
        try {
            Unit unit;
            unit.read(d.filePath(fname));
            m_units[unit.name()] = unit;
        } catch (error::Error e) {
            debug::error("Loading config ", fname);
            debug::error("Error", e.what());
        }
    }
}

bool Config::set(const QVariantMap &data)
{
    bool updated = false;
    Unit unit(data);

    QString name = unit.name();
    QString configPath = path(name);

    if (!os::path::exists(m_unitsDir)) {
        os::mkdir(m_unitsDir);
        m_units[name] = unit;
        updated = true;
    } else if (m_units.contains(name)) {
        updated = m_units[name].update(data);
    } else if (os::path::exists(configPath)) {
        Unit actual = Unit().read(configPath);
        updated = actual.update(data);
        m_units[name] = actual;
    } else {
        m_units[name] = unit;
        updated = true;
    }

    return updated && m_units[name].write(configPath);
}

QString Config::rm(const QString &name)
{
    QString fname = path(name);
    if (!os::path::exists(fname)) {
        return QString();
    }
    os::rm(fname);
    return name + moduleExt;
}

QString Config::path(const QString &fname) const
{
    return m_unitsDir + "/" + fname + moduleExt;
}

QMap<QString, Unit> Config::units() const
{
    return m_units;
}

QString Config::root() const
{
    return m_unitsDir;
}



Vault::Vault(LibGit::Repo *vcs)
     : m_config(QDir(vcs->path() + "/.modules").absolutePath())
     , m_vcs(vcs)
{

}

Vault::~Vault()
{
}

bool Vault::set(const QVariantMap &data)
{
    if (!m_config.set(data)) {
        return false;
    }

    m_vcs->add(m_config.root(), LibGit::AddOptions::All);
    LibGit::RepoStatus status = m_vcs->status(m_config.root());
    if (!status.isClean()) {
        m_vcs->commit("+" + data.value("name").toString());
    }
    return true;
}

bool Vault::rm(const QString &name)
{
    QString fname = m_config.rm(name);
    if (fname.isEmpty()) {
        return false;
    }

    fname = m_config.root() + "/" + fname;
    m_vcs->add(fname, LibGit::AddOptions::Update);
    LibGit::RepoStatus status = m_vcs->status(m_config.root());
    if (status.isClean()) {
        error::raise({{"msg", "Logic error, can't rm vcs path"}, {"path", fname}});
    }
    m_vcs->commit("-" + name);
    return true;
}

bool Vault::update(const QVariantMap &src)
{
    bool updated = false;
    QStringList units = m_config.units().keys();
    for (auto i = src.begin(); i != src.end(); ++i) {
        if (set(i.value().toMap())) {
            updated = true;
        }
    }
    for (const QString &n: units) {
        if (!src.contains(n)) {
            if (!rm(n)) {
                error::raise({{"msg", n + " is not removed??"}});
            }
            updated = true;
        }
    }
    return updated;
}

QMap<QString, Unit> Vault::units() const
{
    return m_config.units();
}

}}