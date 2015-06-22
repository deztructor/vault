#include "vault-sync.hpp"

static Action actionFromName(QString const &name)
{
    Action res;
    if (name == "import")
        res = Action::Import;
    else if (name == "export")
        res = Action::Export;
    else
        error::raise({{"msg", "Parameter 'action' is unknown"}
                , {"action", name}});
    return res;
}

int main_(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("vault-copy");
    parser.addHelpOption();

    parser << QCommandLineOption({"a", "action"},
                                 "The action passed by vault", "ACTION")
         << QCommandLineOption({"L", "dereference"},
                                 "Follow symlinks in src")
         << QCommandLineOption({"n", "no-clobber"},
                                 "Do not overwrite an existing file")
         << QCommandLineOption({"b", "blobs"},
                                 "Use blob mode")
         << QCommandLineOption({"r", "recursive"},
                                 "Copy directories recursively")
        ;
    parser.addPositionalArgument("src", "Source file/directory");
    parser.addPositionalArgument("dst", "Destination file/directory");
    parser.process(app);

    auto args = parser.positionalArguments();
    auto last = args.size() - 1;
    if (last < 1)
        error::raise({{"msg", "There is no src or dst"}, {"args", args}});
    auto dst = args[last];
    auto action = actionFromName(parser.value("action"));
    auto vault = std::make_shared<Vault>(action == Action::Import ? args[0] : dst);
    options_type options{vault
            , parser.isSet("blobs") ? DataHint::Big : DataHint::Compact
            , parser.isSet("recursive") ? Depth::Recursive : Depth::Shallow
            , parser.isSet("no-clobber") ? Overwrite::No : Overwrite::Yes
            , parser.isSet("dereference") ? Deref::Yes : Deref::No
            };

    Processor processor;
    for (auto i = 0; i < last; ++i) {
        auto src = args[i];
        debug::debug("Source", src);
        auto ctx = std::make_shared<context_type>
            (options, action, QFileInfo(src), QFileInfo(dst));
        processor.add(ctx);
    }
    processor.execute();
    return 0;
}

int main(int argc, char **argv)
{
    int res = 1;
    try {
        res = main_(argc, argv);
    } catch (std::exception const &e) {
        debug::error("Error:",  e.what());
    } catch (...) {
        debug::error("Unknown error");
    }
    return res;
}
