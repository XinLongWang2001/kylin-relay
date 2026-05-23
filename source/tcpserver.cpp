#include "tcpserver.h"
#include <QDebug>

TcpServer::TcpServer(const Config &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
    m_server = new QTcpServer(this);
    m_executor = new CommandExecutor(this);

    connect(m_server, &QTcpServer::newConnection, this, &TcpServer::onNewConnection);

    connect(m_executor, &CommandExecutor::stdoutReady, this, &TcpServer::onCmdStdout);
    connect(m_executor, &CommandExecutor::stderrReady, this, &TcpServer::onCmdStderr);
    connect(m_executor, &CommandExecutor::finished, this, &TcpServer::onCmdFinished);
}

bool TcpServer::start()
{
    QHostAddress addr(m_cfg.bindAddress);
    bool ok = m_server->listen(addr, m_cfg.port);
    if (ok) {
        emit logMessage(QStringLiteral("Server started on %1:%2").arg(m_cfg.bindAddress).arg(m_cfg.port));
        emit startedChanged(true);
    } else {
        emit logMessage(QStringLiteral("Failed to start server: %1").arg(m_server->errorString()));
        emit startedChanged(false);
    }
    return ok;
}

void TcpServer::stop()
{
    for (auto *s : m_clients) {
        s->disconnectFromHost();
        s->deleteLater();
    }
    m_clients.clear();
    m_authenticated.clear();
    m_server->close();
    emit logMessage(QStringLiteral("Server stopped"));
    emit connectionCountChanged(0);
    emit startedChanged(false);
}

void TcpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_clients.append(socket);
        connect(socket, &QTcpSocket::readyRead, this, &TcpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &TcpServer::onClientDisconnected);

        QString peer = socket->peerAddress().toString();
        emit logMessage(QStringLiteral("New connection from %1:%2")
                        .arg(peer).arg(socket->peerPort()));
        emit connectionCountChanged(m_clients.size());
    }
}

void TcpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    while (socket->canReadLine()) {
        QByteArray line = socket->readLine().trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) {
            QJsonObject errResp;
            errResp[QStringLiteral("type")] = QStringLiteral("error");
            errResp[QStringLiteral("data")] = QStringLiteral("Invalid JSON: %1").arg(err.errorString());
            sendJson(socket, errResp);
            continue;
        }
        processMessage(socket, doc.object());
    }
}

void TcpServer::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    m_authenticated.remove(socket);
    m_clients.removeOne(socket);
    socket->deleteLater();
    emit logMessage(QStringLiteral("Client disconnected"));
    emit connectionCountChanged(m_clients.size());
}

void TcpServer::processMessage(QTcpSocket *socket, const QJsonObject &msg)
{
    QString type = msg.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("auth")) {
        QString token = msg.value(QStringLiteral("token")).toString();
        if (token == m_cfg.token) {
            m_authenticated.insert(socket);
            QJsonObject resp;
            resp[QStringLiteral("type")] = QStringLiteral("auth_ok");
            sendJson(socket, resp);
            emit logMessage(QStringLiteral("Client authenticated"));
        } else {
            QJsonObject resp;
            resp[QStringLiteral("type")] = QStringLiteral("auth_fail");
            resp[QStringLiteral("data")] = QStringLiteral("Invalid token");
            sendJson(socket, resp);
            emit logMessage(QStringLiteral("Auth failed - bad token"));
        }
        return;
    }

    if (!m_authenticated.contains(socket)) {
        QJsonObject resp;
        resp[QStringLiteral("type")] = QStringLiteral("error");
        resp[QStringLiteral("data")] = QStringLiteral("Not authenticated. Send auth first.");
        sendJson(socket, resp);
        return;
    }

    if (type == QStringLiteral("exec")) {
        QString id = msg.value(QStringLiteral("id")).toString();
        QString command = msg.value(QStringLiteral("command")).toString();
        int timeout = msg.value(QStringLiteral("timeout")).toInt(30);

        if (id.isEmpty() || command.isEmpty()) {
            QJsonObject resp;
            resp[QStringLiteral("type")] = QStringLiteral("error");
            resp[QStringLiteral("data")] = QStringLiteral("Missing id or command");
            sendJson(socket, resp);
            return;
        }

        emit commandStarted(id, command);
        m_executor->execute(id, command, timeout);
        emit logMessage(QStringLiteral("Exec[%1]: %2").arg(id, command));
        return;
    }

    if (type == QStringLiteral("ping")) {
        QJsonObject pong;
        pong[QStringLiteral("type")] = QStringLiteral("pong");
        sendJson(socket, pong);
        return;
    }
}

void TcpServer::onCmdStdout(const QString &id, const QByteArray &data)
{
    for (auto *s : m_authenticated) {
        QJsonObject resp;
        resp[QStringLiteral("id")] = id;
        resp[QStringLiteral("type")] = QStringLiteral("stdout");
        resp[QStringLiteral("data")] = QString::fromUtf8(data);
        sendJson(s, resp);
    }
}

void TcpServer::onCmdStderr(const QString &id, const QByteArray &data)
{
    for (auto *s : m_authenticated) {
        QJsonObject resp;
        resp[QStringLiteral("id")] = id;
        resp[QStringLiteral("type")] = QStringLiteral("stderr");
        resp[QStringLiteral("data")] = QString::fromUtf8(data);
        sendJson(s, resp);
    }
}

void TcpServer::onCmdFinished(const QString &id, int exitCode)
{
    for (auto *s : m_authenticated) {
        QJsonObject resp;
        resp[QStringLiteral("id")] = id;
        resp[QStringLiteral("type")] = QStringLiteral("exit");
        resp[QStringLiteral("code")] = exitCode;
        sendJson(s, resp);
    }
    emit logMessage(QStringLiteral("Exec[%1] finished: exit=%2").arg(id).arg(exitCode));
}

void TcpServer::sendJson(QTcpSocket *socket, const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    socket->write(data);
    socket->flush();
}
