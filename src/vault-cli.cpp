
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "vault.hpp"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("The Vault");
    parser.addHelpOption();

    QCommandLineOption actionOption(QStringList() << "a" << "action", "action", "action");
    parser.addOption(actionOption);
    QCommandLineOption vaultOption(QStringList() << "v" << "vault", "vault", "path");
    parser.addOption(vaultOption);
    QCommandLineOption homeOption(QStringList() << "H" << "home", "home", "home");
    parser.addOption(homeOption);

    parser.process(app);

    vault::Vault vault(parser.value("vault"));
    QString action = parser.value("action");
    if (action == "init") {
        QVariantMap config;
        config.insert("user.name", "Some Sailor");
        qDebug()<<vault.init(config);
    } else if (action == "backup" || action == "export") {
        vault.backup(parser.value(homeOption), QStringList() << "unit1" << "unit2", "");
    } else if (action == "list-snapshots") {
        for (const vault::Snapshot &snapshot: vault.snapshots()) {
            qDebug() << snapshot.tag();
        }
    }


    qDebug()<<parser.value("action");

    return 0;
}
