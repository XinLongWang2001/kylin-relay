#include "commandexecutor.h"
#include <QDebug>

#if defined(Q_OS_WIN)
#  define SHELL_CMD  QStringLiteral("cmd.exe")
#  define SHELL_ARG  QStringLiteral("/c")
#else
#  define SHELL_CMD  QStringLiteral("/bin/sh")
#  define SHELL_ARG  QStringLiteral("-c")
#endif

CommandExecutor::CommandExecutor(QObject *parent) : QObject(parent) {}

void CommandExecutor::execute(const QString &id, const QString &command, int timeoutSec)
{
    if (m_procs.contains(id)) {
        qWarning() << "Duplicate exec id:" << id;
        return;
    }

    ProcEntry entry;
    entry.process = new QProcess(this);
    entry.timer = new QTimer(this);
    entry.timer->setSingleShot(true);

    connect(entry.process, &QProcess::readyReadStandardOutput, this, &CommandExecutor::onReadyReadStdout);
    connect(entry.process, &QProcess::readyReadStandardError, this, &CommandExecutor::onReadyReadStderr);
    connect(entry.process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CommandExecutor::onProcessFinished);
    connect(entry.timer, &QTimer::timeout, this, &CommandExecutor::onTimeout);

    entry.process->setProperty("execId", id);
    entry.timer->setProperty("execId", id);
    entry.timer->start(timeoutSec * 1000);

    m_procs.insert(id, entry);
    entry.process->start(SHELL_CMD, QStringList() << SHELL_ARG << command);
}

void CommandExecutor::cancel(const QString &id)
{
    if (!m_procs.contains(id)) return;
    auto &e = m_procs[id];
    if (e.process && e.process->state() != QProcess::NotRunning) {
        e.process->kill();
        e.process->waitForFinished(3000);
    }
    e.timer->stop();
    m_procs.remove(id);
}

void CommandExecutor::onReadyReadStdout()
{
    QProcess *p = qobject_cast<QProcess*>(sender());
    if (!p) return;
    QString id = p->property("execId").toString();
    QByteArray data = p->readAllStandardOutput();
    if (!data.isEmpty())
        emit stdoutReady(id, data);
}

void CommandExecutor::onReadyReadStderr()
{
    QProcess *p = qobject_cast<QProcess*>(sender());
    if (!p) return;
    QString id = p->property("execId").toString();
    QByteArray data = p->readAllStandardError();
    if (!data.isEmpty())
        emit stderrReady(id, data);
}

void CommandExecutor::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess *p = qobject_cast<QProcess*>(sender());
    if (!p) return;
    QString id = p->property("execId").toString();

    // Read any remaining data
    onReadyReadStdout();
    onReadyReadStderr();

    if (m_procs.contains(id)) {
        m_procs[id].timer->stop();
        m_procs.remove(id);
    }

    int code = (status == QProcess::CrashExit) ? -1 : exitCode;
    emit finished(id, code);
}

void CommandExecutor::onTimeout()
{
    QTimer *t = qobject_cast<QTimer*>(sender());
    if (!t) return;
    QString id = t->property("execId").toString();
    emit stderrReady(id, QByteArray("ERROR: Command timed out\n"));

    if (m_procs.contains(id) && m_procs[id].process) {
        m_procs[id].process->kill();
    }
    emit finished(id, -1);
    if (m_procs.contains(id)) {
        m_procs.remove(id);
    }
}
