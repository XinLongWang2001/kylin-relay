#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMap>
#include "tcpserver.h"
#include "config.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const Config &cfg, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStop();
    void onLogMessage(const QString &msg);
    void onConnCountChanged(int count);
    void onServerStatusChanged(bool running);
    void onRowSelected(int row, int col);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onExecStdout(const QString &id, const QByteArray &data);
    void onExecFinished(const QString &id, int exitCode);

private:
    void setupUI();
    void setupTray();
    void loadHistory();
    void addHistoryEntry(const QString &id, const QString &cmd, int exitCode, const QString &output);
    void updateStatusLabel();

    Config m_cfg;
    TcpServer *m_server;

    // Control bar
    QPushButton *m_btnStartStop;
    QLabel *m_lblStatus;
    QLabel *m_lblConnCount;
    QLabel *m_lblPort;

    // Command log
    QTableWidget *m_table;
    QTextEdit *m_outputView;

    // History data
    struct HistoryEntry {
        QString id;
        QString command;
        QString output;
        int exitCode;
    };
    QList<HistoryEntry> m_history;
    int m_selectedRow = -1;
    QMap<QString, QString> m_outputBuffer;
    QMap<QString, QString> m_cmdBuffer;

    // Tray
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
};

#endif // MAINWINDOW_H
