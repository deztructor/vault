#include "vault-util.hpp"

#define QS_(s) QLatin1String(s)

int main_(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription
        (QS_("vault-resolve is used to resolve URI stored in the "
             "blob reference file to the blob path in the vault storage.\n"
             "\tAlso, if option 'vault' is provided, it converts passed URI to the blob path."));
    parser.addHelpOption();

    parser << QCommandLineOption({QS_("V"), QS_("vault")},
                                 QS_("Vault root directory"), QS_("vault"))
         << QCommandLineOption({QS_("R"), QS_("reverse")},
                                 QS_("Generate URI from the blob path"))
        ;
    parser.addPositionalArgument(QS_("src"), QS_("Blob reference file path or uri"));
    parser.process(app);
    auto args = parser.positionalArguments();
    if (args.size() < 1) {
        parser.showHelp();
        error::raise({{"msg", "Parameter is missing"}});
    }
    QString root, uri, src;
    auto is_reverse = parser.isSet("reverse");
    if (parser.isSet("vault")) {
        root = parser.value(QS_("vault"));
        uri = args.at(0);
        if (is_reverse)
            src = args.at(0);
    } else {
        if (is_reverse) {
            parser.showHelp();
            error::raise({{"msg", "Reverse conversion requires vault path"}});
        }
        src = args.at(0);
        uri = read_text(src, VAULT_URI_MAX_SIZE).trimmed();
        root = src;
    }
    auto vault = std::make_shared<Vault>(root);
    QTextStream cout{stdout};
    if (is_reverse) {
        cout << vault->uri_from_hash(vault->blob_hash(src));
    } else {
        cout << vault->path_from_uri(uri.trimmed());
    }
    return 0;
}

int main(int argc, char **argv)
{
    int res = 1;
    try {
        res = main_(argc, argv);
    } catch (std::exception const &e) {
        debug::error(QS_("vault-resolve"), QS_("Error:"),  QS_(e.what()));
    } catch (...) {
        debug::error(QS_("vault-resolve"), QS_("Unknown error"));
    }
    return res;
}
