#ifndef COMMANDEXECUTOR_H
#define COMMANDEXECUTOR_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QTimer>

class CommandExecutor : public QObject
{
    Q_OBJECT
public:
    explicit CommandExecutor(QObject *parent = nullptr);
    void execute(const QString &id, const QString &command, int timeoutSec = 30);
    void cancel(const QString &id);

signals:
    void stdoutReady(const QString &id, const QByteArray &data);
    void stderrReady(const QString &id, const QByteArray &data);
    void finished(const QString &id, int exitCode);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onTimeout();

private:
    struct ProcEntry {
        QProcess *process = nullptr;
        QTimer *timer = nullptr;
    };
    QMap<QString, ProcEntry> m_procs;
};

#endif // COMMANDEXECUTOR_H
