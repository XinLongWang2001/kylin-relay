#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QSettings>

struct Config {
    quint16 port = 12345;
    QString token = QStringLiteral("qclaw-relay");
    QString bindAddress = QStringLiteral("127.0.0.1");

    void load() {
        // QSettings(IniFormat, UserScope, org, app) picks the platform-native
        // config directory automatically:
        //   Linux:   ~/.config/QClaw/qt-relay.ini
        //   Windows: %APPDATA%\QClaw\qt-relay.ini
        //   macOS:   ~/Library/Preferences/QClaw/qt-relay.ini
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("QClaw"), QStringLiteral("qt-relay"));
        port = static_cast<quint16>(s.value(QStringLiteral("port"), port).toUInt());
        token = s.value(QStringLiteral("token"), token).toString();
        bindAddress = s.value(QStringLiteral("bind"), bindAddress).toString();
    }

    void save() {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("QClaw"), QStringLiteral("qt-relay"));
        s.setValue(QStringLiteral("port"), port);
        s.setValue(QStringLiteral("token"), token);
        s.setValue(QStringLiteral("bind"), bindAddress);
    }
};

#endif // CONFIG_H
