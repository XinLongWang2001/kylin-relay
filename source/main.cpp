#include <QApplication>
#include <QCommandLineParser>
#include "mainwindow.h"
#include "config.h"

static QString envOr(const char *key, const QString &fallback)
{
    QByteArray val = qgetenv(key);
    return val.isEmpty() ? fallback : QString::fromUtf8(val);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("qt-relay");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("QClaw");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt Relay - TCP command relay service with GUI");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOpt("port", "TCP listen port", "port");
    QCommandLineOption tokenOpt("token", "Auth token", "token");
    QCommandLineOption bindOpt("bind", "Bind address", "address");
    parser.addOption(portOpt);
    parser.addOption(tokenOpt);
    parser.addOption(bindOpt);
    parser.process(app);

    // Priority: CLI args > environment variables > QSettings > defaults
    Config cfg;
    cfg.load(); // QSettings

    if (!envOr("QT_RELAY_PORT", QString()).isEmpty())
        cfg.port = envOr("QT_RELAY_PORT", QString()).toUShort();
    if (!envOr("QT_RELAY_TOKEN", QString()).isEmpty())
        cfg.token = envOr("QT_RELAY_TOKEN", QString());
    if (!envOr("QT_RELAY_BIND", QString()).isEmpty())
        cfg.bindAddress = envOr("QT_RELAY_BIND", QString());

    if (parser.isSet(portOpt))
        cfg.port = parser.value(portOpt).toUShort();
    if (parser.isSet(tokenOpt))
        cfg.token = parser.value(tokenOpt);
    if (parser.isSet(bindOpt))
        cfg.bindAddress = parser.value(bindOpt);

    MainWindow w(cfg);
    w.show();
    return app.exec();
}
