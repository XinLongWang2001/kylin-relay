#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include "commandexecutor.h"
#include "config.h"

class TcpServer : public QObject
{
    Q_OBJECT
public:
    explicit TcpServer(const Config &cfg, QObject *parent = nullptr);
    bool start();
    void stop();
    bool isRunning() const { return m_server->isListening(); }
    CommandExecutor* executor() const { return m_executor; }
    int connectionCount() const { return m_clients.size(); }

signals:
    void logMessage(const QString &msg);
    void connectionCountChanged(int count);
    void startedChanged(bool running);
    void commandStarted(const QString &id, const QString &command);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();
    void onCmdStdout(const QString &id, const QByteArray &data);
    void onCmdStderr(const QString &id, const QByteArray &data);
    void onCmdFinished(const QString &id, int exitCode);

private:
    void sendJson(QTcpSocket *socket, const QJsonObject &obj);
    void processMessage(QTcpSocket *socket, const QJsonObject &msg);

    QTcpServer *m_server;
    QList<QTcpSocket*> m_clients;
    QSet<QTcpSocket*> m_authenticated;
    CommandExecutor *m_executor;
    Config m_cfg;
};

#endif // TCPSERVER_H
