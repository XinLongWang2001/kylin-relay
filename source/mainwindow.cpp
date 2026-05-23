#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QDateTime>
#include <QCloseEvent>
#include <QStatusBar>
#include <QColor>
#include <QFont>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>

MainWindow::MainWindow(const Config &cfg, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg)
{
    m_server = new TcpServer(m_cfg, this);
    connect(m_server, &TcpServer::logMessage, this, &MainWindow::onLogMessage);
    connect(m_server, &TcpServer::connectionCountChanged, this, &MainWindow::onConnCountChanged);
    connect(m_server, &TcpServer::startedChanged, this, &MainWindow::onServerStatusChanged);

    // Connect to command execution signals for history tracking
    CommandExecutor *exec = m_server->executor();
    connect(m_server, &TcpServer::commandStarted, this, [this](const QString &id, const QString &cmd) {
        m_cmdBuffer[id] = cmd;
    });
    connect(exec, &CommandExecutor::stdoutReady, this, &MainWindow::onExecStdout);
    connect(exec, &CommandExecutor::finished, this, &MainWindow::onExecFinished);

    setupUI();
    setupTray();
    loadHistory();

    setWindowTitle(QStringLiteral("Qt Relay - 命令中转服务"));
    resize(900, 600);

    // Auto-start server
    m_server->start();
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon->isVisible()) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::setupUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // === Control bar ===
    QHBoxLayout *ctrlBar = new QHBoxLayout();

    m_lblStatus = new QLabel(QStringLiteral("● 已启动"), this);
    m_lblStatus->setStyleSheet(QStringLiteral("color: #27ae60; font-weight: bold; font-size: 14px;"));

    m_lblPort = new QLabel(QStringLiteral("端口: %1").arg(m_cfg.port), this);
    m_lblConnCount = new QLabel(QStringLiteral("连接: 0"), this);

    m_btnStartStop = new QPushButton(QStringLiteral("停止"), this);
    m_btnStartStop->setFixedWidth(80);
    connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::onStartStop);

    ctrlBar->addWidget(m_lblStatus);
    ctrlBar->addSpacing(20);
    ctrlBar->addWidget(m_lblPort);
    ctrlBar->addSpacing(10);
    ctrlBar->addWidget(m_lblConnCount);
    ctrlBar->addStretch();
    ctrlBar->addWidget(m_btnStartStop);
    mainLayout->addLayout(ctrlBar);

    // === Splitter: command table + output ===
    QSplitter *splitter = new QSplitter(Qt::Vertical, this);

    // Command table
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("时间"),
        QStringLiteral("命令"),
        QStringLiteral("退出码"),
        QStringLiteral("ID")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellClicked, this, &MainWindow::onRowSelected);

    // Output viewer
    m_outputView = new QTextEdit(this);
    m_outputView->setReadOnly(true);
    QFont monoFont(QStringLiteral("Courier"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_outputView->setFont(monoFont);
    m_outputView->setPlaceholderText(QStringLiteral("选择一条命令查看输出..."));

    splitter->addWidget(m_table);
    splitter->addWidget(m_outputView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter);
}

void MainWindow::setupTray()
{
    m_trayIcon = new QSystemTrayIcon(
        style()->standardIcon(QStyle::SP_ComputerIcon), this);

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(QStringLiteral("显示"), this, &QMainWindow::show);
    m_trayMenu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip(QStringLiteral("Qt Relay - 命令中转服务"));
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_trayIcon->show();
}

void MainWindow::onStartStop()
{
    if (m_server->isRunning()) {
        m_server->stop();
    } else {
        m_server->start();
    }
}

void MainWindow::onLogMessage(const QString &msg)
{
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::onConnCountChanged(int count)
{
    m_lblConnCount->setText(QStringLiteral("连接: %1").arg(count));
}

void MainWindow::onServerStatusChanged(bool running)
{
    if (running) {
        m_lblStatus->setText(QStringLiteral("● 已启动"));
        m_lblStatus->setStyleSheet(QStringLiteral("color: #27ae60; font-weight: bold; font-size: 14px;"));
        m_btnStartStop->setText(QStringLiteral("停止"));
    } else {
        m_lblStatus->setText(QStringLiteral("● 已停止"));
        m_lblStatus->setStyleSheet(QStringLiteral("color: #e74c3c; font-weight: bold; font-size: 14px;"));
        m_btnStartStop->setText(QStringLiteral("启动"));
    }
}

void MainWindow::onRowSelected(int row, int col)
{
    Q_UNUSED(col);
    if (row < 0 || row >= m_history.size()) return;
    m_selectedRow = row;
    m_outputView->setPlainText(m_history[row].output);
}

void MainWindow::addHistoryEntry(const QString &id, const QString &cmd, int exitCode, const QString &output)
{
    HistoryEntry entry;
    entry.id = id;
    entry.command = cmd;
    entry.output = output;
    entry.exitCode = exitCode;
    m_history.append(entry);

    int row = m_table->rowCount();
    m_table->insertRow(row);

    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_table->setItem(row, 0, new QTableWidgetItem(ts));
    m_table->setItem(row, 1, new QTableWidgetItem(cmd));

    QString exitStr = (exitCode == 0)
        ? QStringLiteral("✓ %1").arg(exitCode)
        : QStringLiteral("✗ %1").arg(exitCode);
    QTableWidgetItem *exitItem = new QTableWidgetItem(exitStr);
    exitItem->setForeground(exitCode == 0 ? QColor(QStringLiteral("#27ae60")) : QColor(QStringLiteral("#e74c3c")));
    m_table->setItem(row, 2, exitItem);
    m_table->setItem(row, 3, new QTableWidgetItem(id));

    m_table->scrollToBottom();
}

void MainWindow::loadHistory()
{
    // History loaded from server signals during execution
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::onExecStdout(const QString &id, const QByteArray &data)
{
    m_outputBuffer[id].append(QString::fromUtf8(data));
}

void MainWindow::onExecFinished(const QString &id, int exitCode)
{
    QString cmd = m_cmdBuffer.value(id, QStringLiteral("?"));
    QString output = m_outputBuffer.value(id);
    addHistoryEntry(id, cmd, exitCode, output);
    m_outputBuffer.remove(id);
    m_cmdBuffer.remove(id);
}

void MainWindow::updateStatusLabel()
{
    onServerStatusChanged(m_server->isRunning());
}
