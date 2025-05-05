// Copyright (C) [2025] [@kleedaisuki] <kleedaisuki@outlook.com>
// This file is part of Simple‑K Cloud Executor.
//
// Simple‑K Cloud Executor is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// Simple‑K Cloud Executor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// Simple‑K Cloud Executor.  If not, see <https://www.gnu.org/licenses/>.
//
// ──────────────────────────────────────────────────────────────────────────────
//
// This file links against the Qt 6.9.0 framework,
// available under the GNU Lesser General Public License v3.
// Source code for Qt can be obtained from:
//   https://download.qt.io/official_releases/qt/6.9/6.9.0/
//
// In compliance with LGPL v3 §4, users may replace the linked Qt libraries
// and run the modified version.  Installation information is provided in
// the project README.
//

#include "./network/network.hpp"

#include <QApplication>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>

enum TaskStatus
{
    InProgress,
    Completed,
    Error
};
const int TaskStatusRole = Qt::UserRole + 1;
const int FilePathRole = Qt::UserRole + 2;
const int ErrorMsgRole = Qt::UserRole + 3;
const int TaskIdRole = Qt::UserRole + 4;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ClientSocket &clientSocket, QWidget *parent = nullptr);
    ~MainWindow() override = default;
    void handleReceivedFileDataAsync(const string &originalFileName, const vector<char> &fileData);

private slots:
    void on_treeView_clicked(const QModelIndex &index);
    void on_listView_doubleClicked(const QModelIndex &index);
    void goUp();
    void on_pathLineEdit_returnPressed();
    void on_listView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

    void on_sendAction_triggered();
    void on_taskListItem_doubleClicked(QListWidgetItem *item);
    void on_clearTasksButton_clicked();
    void updateTaskItem(QListWidgetItem *item, const QString &filePath, TaskStatus status, const QString &message);
    void handleSendFileResult(qint64 taskId, const QString filePath, bool success, const QString errorMsg = "");
    void handleFileReceptionResult(const QString originalFileName, const QString savedFilePath, bool success, const QString errorMsg = "");

    void updateUiWithPathInfo(const QString path, bool success, const QString errorMsg = "");
    void handleOpenFileResult(const QString filePath, bool success);

private:
    void setupUi();
    void setupActionsAndToolbar();
    void setupTaskArea(QWidget *parentWidget);

    void startNavigateToPath(const QString &path);
    pair<bool, QString> performNavigation(QString path);
    void startOpenFile(const QString &filePath);

    void startSendFile(const QString &filePath);
    QListWidgetItem *findTaskItemById(qint64 taskId);
    void removeTaskFuture(qint64 taskId);

    QFileSystemModel *treeViewModel = nullptr;
    QFileSystemModel *listViewModel = nullptr;
    QTreeView *treeView = nullptr;
    QListView *listView = nullptr;
    QLineEdit *pathLineEdit = nullptr;
    QSplitter *mainSplitter = nullptr;
    QSplitter *rightSplitter = nullptr;
    QListWidget *taskListWidget = nullptr;
    QPushButton *clearTasksButton = nullptr;
    QToolBar *toolBar = nullptr;

    QAction *upAction = nullptr;
    QAction *sendAction = nullptr;

    ClientSocket &clientSocketInstance_;
    map<qint64, future<void>> activeTasks_;
    mutex taskMapMutex_;

    future<void> navigationFuture;
    future<void> openFileFuture;
    const QString outputDirectory = "out";
    atomic<qint64> nextTaskId_ = 0;
};

MainWindow::MainWindow(ClientSocket &clientSocket, QWidget *parent)
    : QMainWindow(parent), clientSocketInstance_(clientSocket)
{
    setupUi();
    log_write_regular_information("MainWindow created successfully.");
    QDir dir;
    if (!dir.exists(outputDirectory))
    {
        if (!dir.mkpath(outputDirectory))
        {
            log_write_error_information("Failed to create output directory: " + outputDirectory.toStdString());
            QMessageBox::critical(this, "错误", "无法创建输出目录 'out'！");
        }
    }
}

void MainWindow::setupUi()
{
    treeViewModel = new QFileSystemModel(this);
    listViewModel = new QFileSystemModel(this);
    treeViewModel->setRootPath("");
    treeViewModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    listViewModel->setRootPath("");
    listViewModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    log_write_regular_information("FileSystemModels initialized.");

    treeView = new QTreeView(this);
    treeView->setModel(treeViewModel);
    for (int i = 1; i < treeViewModel->columnCount(); ++i)
        treeView->hideColumn(i);
    treeView->setRootIndex(treeViewModel->index(treeViewModel->rootPath()));
    treeView->setSortingEnabled(true);

    listView = new QListView(this);
    listView->setModel(listViewModel);
    listView->setViewMode(QListView::IconMode);
    listView->setGridSize(QSize(80, 80));
    listView->setResizeMode(QListView::Adjust);
    listView->setMovement(QListView::Static);
    listView->setSelectionMode(QAbstractItemView::SingleSelection);
    listView->setUniformItemSizes(true);

    pathLineEdit = new QLineEdit(this);

    QWidget *rightPaneWidget = new QWidget(this);
    QVBoxLayout *rightPaneLayout = new QVBoxLayout(rightPaneWidget);
    rightPaneLayout->setContentsMargins(0, 0, 0, 0);
    rightPaneLayout->setSpacing(0);
    rightSplitter = new QSplitter(Qt::Vertical, rightPaneWidget);
    rightSplitter->addWidget(listView);
    QWidget *taskAreaWidget = new QWidget(rightPaneWidget);
    setupTaskArea(taskAreaWidget);
    rightSplitter->addWidget(taskAreaWidget);
    rightSplitter->setStretchFactor(0, 7);
    rightSplitter->setStretchFactor(1, 3);
    rightPaneLayout->addWidget(rightSplitter);

    mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(treeView);
    mainSplitter->addWidget(rightPaneWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addWidget(pathLineEdit);
    mainLayout->addWidget(mainSplitter);
    setCentralWidget(centralWidget);

    setupActionsAndToolbar();
    log_write_regular_information("UI components created and laid out.");

    connect(treeView, &QTreeView::clicked, this, &MainWindow::on_treeView_clicked);
    connect(listView, &QListView::doubleClicked, this, &MainWindow::on_listView_doubleClicked);
    connect(listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::on_listView_selectionChanged);
    connect(upAction, &QAction::triggered, this, &MainWindow::goUp);
    connect(sendAction, &QAction::triggered, this, &MainWindow::on_sendAction_triggered);
    connect(pathLineEdit, &QLineEdit::returnPressed, this, &MainWindow::on_pathLineEdit_returnPressed);
    connect(taskListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::on_taskListItem_doubleClicked);
    connect(clearTasksButton, &QPushButton::clicked, this, &MainWindow::on_clearTasksButton_clicked);
    log_write_regular_information("Signal-slot connections established.");

    setWindowTitle("File Explorer v2.7");
    resize(900, 700);

    QString initialPath = QDir::homePath();
    if (initialPath.isEmpty() || !QDir(initialPath).exists())
    {
        log_write_warning_information("Home path is invalid or empty. Falling back to current path.");
        initialPath = QDir::currentPath();
    }
    if (initialPath.isEmpty())
    {
        log_write_error_information("Initial path is still empty after fallback! Using root path.");
        initialPath = QDir::rootPath();
    }
    log_write_regular_information("Starting initial navigation to user home ('~'): " + initialPath.toStdString());
    startNavigateToPath(initialPath);
}

void MainWindow::setupActionsAndToolbar()
{
    upAction = new QAction("Up", this);
    upAction->setToolTip("Go to parent directory");
    sendAction = new QAction("Send", this);
    sendAction->setToolTip("Send the selected file");
    sendAction->setEnabled(false);
    toolBar = addToolBar("Main Toolbar");
    toolBar->addAction(upAction);
    toolBar->addAction(sendAction);
    log_write_regular_information("Actions and Toolbar created.");
}

void MainWindow::setupTaskArea(QWidget *parentWidget)
{
    QVBoxLayout *taskLayout = new QVBoxLayout(parentWidget);
    taskLayout->setContentsMargins(5, 5, 5, 5);
    QLabel *taskLabel = new QLabel("网络任务列表:", parentWidget);
    taskLayout->addWidget(taskLabel);
    taskListWidget = new QListWidget(parentWidget);
    taskListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    taskListWidget->setAlternatingRowColors(true);
    taskLayout->addWidget(taskListWidget);
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    clearTasksButton = new QPushButton("清除已完成/错误", parentWidget);
    clearTasksButton->setToolTip("清除列表中所有已完成或错误的任务");
    buttonLayout->addWidget(clearTasksButton);
    taskLayout->addLayout(buttonLayout);
    log_write_regular_information("Task Area UI created.");
}

void MainWindow::on_treeView_clicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QString path = treeViewModel->filePath(index);
    if (path.isEmpty())
        return;
    startNavigateToPath(path);
}

void MainWindow::on_listView_doubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QFileInfo fileInfo = listViewModel->fileInfo(index);
    if (fileInfo.isDir())
    {
        startNavigateToPath(fileInfo.absoluteFilePath());
    }
    else if (fileInfo.isFile())
    {
        startOpenFile(fileInfo.absoluteFilePath());
    }
}

void MainWindow::goUp()
{
    QString currentPath = listViewModel->filePath(listView->rootIndex());
    if (currentPath.isEmpty())
        return;
    QDir dir(currentPath);
    if (dir.cdUp())
    {
        startNavigateToPath(dir.absolutePath());
    }
}

void MainWindow::on_pathLineEdit_returnPressed()
{
    startNavigateToPath(pathLineEdit->text());
}

void MainWindow::on_listView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList indexes = listView->selectionModel()->selectedIndexes();
    bool canSend = (indexes.count() == 1 && listViewModel->fileInfo(indexes.first()).isFile());
    sendAction->setEnabled(canSend);
}

void MainWindow::on_sendAction_triggered()
{
    bool proceedToSend = false;
    if (clientSocketInstance_.is_connected())
    {
        proceedToSend = true;
    }
    else
    {
        log_write_warning_information("Send action triggered, but client is not connected. Attempting to connect silently...");
        if (clientSocketInstance_.connect())
        {
            log_write_regular_information("Connection attempt initiated successfully.");
            proceedToSend = true;
        }
        else
        {
            log_write_error_information("Failed to initiate connection attempt.");
            QMessageBox::critical(this, "网络错误", "尝试连接服务器失败！请检查网络或服务器状态。");
            proceedToSend = false;
        }
    }

    if (proceedToSend)
    {
        const QModelIndexList indexes = listView->selectionModel()->selectedIndexes();
        if (indexes.count() == 1)
        {
            QFileInfo fileInfo = listViewModel->fileInfo(indexes.first());
            if (fileInfo.isFile())
            {
                startSendFile(fileInfo.absoluteFilePath());
            }
        }
    }
}

void MainWindow::on_taskListItem_doubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    TaskStatus status = static_cast<TaskStatus>(item->data(TaskStatusRole).toInt());
    QString filePath = item->data(FilePathRole).toString();
    log_write_regular_information("Task item double-clicked. Status: " + to_string(status) + ", Path: " + filePath.toStdString());
    if (status == Completed)
    {
        if (filePath.isEmpty())
        {
            QMessageBox::warning(this, "错误", "任务项没有关联的文件路径。");
            return;
        }
        QFileInfo checkFile(filePath);
        if (!checkFile.exists() || !checkFile.isFile())
        {
            QMessageBox::warning(this, "错误", "找不到关联的文件或它不是一个文件:\n" + filePath);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)))
            QMessageBox::warning(this, "无法打开", "无法使用默认应用程序打开文件:\n" + filePath);
    }
    else if (status == Error)
    {
        QString errorMsg = item->data(ErrorMsgRole).toString();
        QMessageBox::warning(this, "任务错误", "此任务执行失败:\n" + (errorMsg.isEmpty() ? "未提供具体信息。" : errorMsg));
    }
    else
    {
        qint64 taskId = item->data(TaskIdRole).toLongLong();
        lock_guard lock(taskMapMutex_);
        if (activeTasks_.count(taskId))
            QMessageBox::information(this, "任务进行中", "此任务仍在进行中...");
        else
            QMessageBox::information(this, "任务状态未知", "任务正在进行但状态跟踪丢失?");
    }
}

void MainWindow::on_clearTasksButton_clicked()
{
    log_write_regular_information("Clear tasks button clicked.");
    for (int i = taskListWidget->count() - 1; i >= 0; --i)
    {
        QListWidgetItem *item = taskListWidget->item(i);
        if (item)
        {
            TaskStatus status = static_cast<TaskStatus>(item->data(TaskStatusRole).toInt());
            if (status == Completed || status == Error)
            {
                log_write_regular_information("Removing task item (row " + to_string(i) + "): " + item->text().toStdString());
                delete taskListWidget->takeItem(i);
            }
        }
    }
    log_write_regular_information("Finished clearing completed/error tasks.");
}

void MainWindow::updateTaskItem(QListWidgetItem *item,
                                const QString &filePath,
                                TaskStatus status,
                                const QString &message)
{
    if (!item)
    {
        log_write_error_information("updateTaskItem called with null item pointer for: " + filePath.toStdString());
        return;
    }
    QString statusPrefix;
    switch (status)
    {
    case InProgress:
        statusPrefix = "⏳ ";
        break;
    case Completed:
        statusPrefix = "✅ ";
        break;
    case Error:
        statusPrefix = "❌ ";
        break;
    }
    item->setText(statusPrefix + message);
    item->setData(TaskStatusRole, status);
    item->setData(FilePathRole, filePath);
    if (status == Error)
    {
        item->setData(ErrorMsgRole, message);
        item->setToolTip("错误: " + message);
    }
    else
    {
        item->setData(ErrorMsgRole, QVariant());
        item->setToolTip(filePath);
    }
    log_write_regular_information("Updated task item. Status: " + to_string(status) + ", Msg: " + message.toStdString());
}

void MainWindow::handleSendFileResult(qint64 taskId, const QString filePath, bool success, const QString errorMsg)
{
    log_write_regular_information("Main thread received send file initiation result for task ID: " + to_string(taskId) + ", Path: " + filePath.toStdString() + ", Success: " + (success ? "true" : "false"));

    QListWidgetItem *itemToUpdate = findTaskItemById(taskId);

    if (!itemToUpdate)
    {
        log_write_error_information("Send result received, but task item not found for task ID: " + to_string(taskId));
        removeTaskFuture(taskId);
        return;
    }

    if (success)
    {
        updateTaskItem(itemToUpdate, filePath, InProgress, "发送中: " + QFileInfo(filePath).fileName());
        log_write_warning_information("Send task (ID: " + to_string(taskId) + ") marked as InProgress. True completion status requires server ACK or better client feedback.");
    }
    else
    {
        updateTaskItem(itemToUpdate, filePath, Error, errorMsg.isEmpty() ? "发送启动失败" : errorMsg);
        QMessageBox::warning(this, "发送失败", "无法启动文件发送: " + QFileInfo(filePath).fileName() + "\n" + errorMsg);
        removeTaskFuture(taskId);
    }
}

void MainWindow::handleFileReceptionResult(const QString originalFileName, const QString savedFilePath, bool success, const QString errorMsg)
{
    log_write_regular_information("Main thread received save file result for original: " + originalFileName.toStdString() + ", Saved to: " + savedFilePath.toStdString() + ", Success: " + (success ? "true" : "false"));
    QListWidgetItem *newItem = new QListWidgetItem();
    taskListWidget->addItem(newItem);
    if (success)
    {
        QString message = QString("接收完成: %1 -> %2").arg(originalFileName, QFileInfo(savedFilePath).fileName());
        updateTaskItem(newItem, savedFilePath, Completed, message);
    }
    else
    {
        QString message = QString("接收失败: %1. 原因: %2").arg(originalFileName, errorMsg.isEmpty() ? "保存文件时出错" : errorMsg);
        updateTaskItem(newItem, originalFileName, Error, message);
        QMessageBox::warning(this, "接收失败", message);
    }
}

void MainWindow::startNavigateToPath(const QString &path)
{
    if (path.isEmpty())
        return;
    if (navigationFuture.valid() && navigationFuture.wait_for(chrono::seconds(0)) == future_status::timeout)
    {
        log_write_warning_information("Previous navigation task still running.");
    }
    navigationFuture = ThreadPool::instance().enqueue(0, [this, path]()
                                                      {
        auto result = performNavigation(path);
        QMetaObject::invokeMethod(this, "updateUiWithPathInfo", Qt::QueuedConnection,
                                  Q_ARG(QString, path), Q_ARG(bool, result.first), Q_ARG(QString, result.second)); });
}

pair<bool, QString> MainWindow::performNavigation(QString path)
{
    QFileInfo pathInfo(path);
    if (!pathInfo.exists())
    {
        return {false, "Path does not exist or is inaccessible"};
    }
    return {true, ""};
}

void MainWindow::updateUiWithPathInfo(const QString path, bool success, const QString errorMsg)
{
    if (success)
    {
        QModelIndex listIdx = listViewModel->index(path);
        if (listIdx.isValid())
            listView->setRootIndex(listIdx);
        else
            log_write_error_information("Failed to get valid listViewModel index for path: " + path.toStdString());
        QModelIndex treeIdx = treeViewModel->index(path);
        if (treeIdx.isValid())
            treeView->expand(treeIdx);
        else
            log_write_warning_information("Path not found or not a directory in tree view model: " + path.toStdString());
        pathLineEdit->setText(QDir::toNativeSeparators(path));
        QDir currentDir(path);
        upAction->setEnabled(currentDir.cdUp());
        listView->clearSelection();
        sendAction->setEnabled(false);
    }
    else
    {
        QMessageBox::warning(this, "路径无效", "无法导航到路径：" + path + "\n" + errorMsg);
        QString previousValidPath = listViewModel->filePath(listView->rootIndex());
        pathLineEdit->setText(previousValidPath.isEmpty() ? QDir::homePath() : QDir::toNativeSeparators(previousValidPath));
    }
}

void MainWindow::startOpenFile(const QString &filePath)
{
    if (openFileFuture.valid() && openFileFuture.wait_for(chrono::seconds(0)) == future_status::timeout)
    {
        log_write_warning_information("Previous open file task still running.");
    }
    openFileFuture = ThreadPool::instance().enqueue(1, [this, filePath]()
                                                    {
        bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        QMetaObject::invokeMethod(this, "handleOpenFileResult", Qt::QueuedConnection,
                                  Q_ARG(QString, filePath), Q_ARG(bool, success)); });
}

void MainWindow::handleOpenFileResult(const QString filePath, bool success)
{
    if (!success)
    {
        log_write_error_information("Failed to open file: " + filePath.toStdString());
        QMessageBox::warning(this, "无法打开文件", "无法使用关联程序打开:\n" + filePath);
    }
}

void MainWindow::startSendFile(const QString &filePath)
{
    QListWidgetItem *newItem = new QListWidgetItem();
    qint64 taskId = nextTaskId_++;
    newItem->setData(TaskIdRole, taskId);
    taskListWidget->addItem(newItem);
    updateTaskItem(newItem, filePath, InProgress, "准备发送: " + QFileInfo(filePath).fileName());

    log_write_regular_information("Enqueueing send file task (ID: " + to_string(taskId) + ") for: " + filePath.toStdString());
    string filePathStd = filePath.toStdString();

    future<void> taskFuture = ThreadPool::instance().enqueue(0, [this, taskId, filePath, filePathStd]() mutable
                                                             {
        log_write_regular_information("Background thread attempting to send file (Task ID: " + to_string(taskId) + "): " + filePathStd);
        bool success = false;
        QString errorMsg = "";
        bool connectionChecked = false;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            if (clientSocketInstance_.is_connected())
            {
                connectionChecked = true;
                break;
            }
            log_write_warning_information("Background task (ID: " + to_string(taskId) + ") waiting for connection... Attempt " + to_string(attempt + 1));
            this_thread::sleep_for(chrono::milliseconds(500));
        }

        if (!connectionChecked || !clientSocketInstance_.is_connected())
        {
            errorMsg = "Client is not connected (checked in background).";
            log_write_error_information(errorMsg.toStdString() + " (Task ID: " + to_string(taskId) + ")");
        }
        else
        {
            try
            {
                success = clientSocketInstance_.send_file("compile-execute", filePathStd);
                if (!success)
                {
                    errorMsg = "send_file function returned false.";
                    log_write_error_information(errorMsg.toStdString() + " Path: " + filePathStd + " (Task ID: " + to_string(taskId) + ")");
                }
                else
                {
                    log_write_regular_information("send_file call successful for task ID: " + to_string(taskId));
                }
            }
            catch (const exception &e)
            {
                errorMsg = QString("Exception during send_file: %1").arg(e.what());
                log_write_error_information(errorMsg.toStdString() + " Path: " + filePathStd + " (Task ID: " + to_string(taskId) + ")");
                success = false;
            }
            catch (...)
            {
                errorMsg = "Unknown exception during send_file.";
                log_write_error_information(errorMsg.toStdString() + " Path: " + filePathStd + " (Task ID: " + to_string(taskId) + ")");
                success = false;
            }
        }

        QMetaObject::invokeMethod(this, "handleSendFileResult", Qt::QueuedConnection,
                                Q_ARG(qint64, taskId),
                                Q_ARG(QString, filePath),
                                Q_ARG(bool, success),
                                Q_ARG(QString, errorMsg));});

    {
        lock_guard lock(taskMapMutex_);
        activeTasks_[taskId] = std::move(taskFuture);
        log_write_regular_information("Stored future for task ID: " + to_string(taskId));
    }
}

void MainWindow::handleReceivedFileDataAsync(const string &originalFileName, const vector<char> &fileData)
{
    log_write_regular_information("Received file data for: " + originalFileName + ", size: " + to_string(fileData.size()) + ". Enqueuing save task.");

    QString qOriginalFileName = QString::fromStdString(originalFileName);
    qint64 taskId = nextTaskId_++;

    future<void> saveFuture = ThreadPool::instance().enqueue(1, [this, taskId, qOriginalFileName, fileData]() {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
        QString savePath = QString("%1/%2.ret").arg(outputDirectory, timestamp);
        log_write_regular_information("Background save task (ID: " + to_string(taskId) + ") attempting to save received file to: " + savePath.toStdString());

        bool success = false;
        QString errorMsg = "";
        try
        {
            QFile file(savePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                errorMsg = QString("Failed to open file for writing: %1. Error: %2").arg(savePath, file.errorString());
            }
            else
            {
                qint64 bytesWritten = file.write(fileData.data(), fileData.size());
                file.close();
                if (bytesWritten != static_cast<qint64>(fileData.size()))
                {
                    errorMsg = QString("Failed to write all data to file: %1. Written: %2, Expected: %3").arg(savePath).arg(bytesWritten).arg(fileData.size());
                    QFile::remove(savePath);
                }
                else
                {
                    success = true;
                }
            }
        }
        catch (const exception &e)
        {
            errorMsg = QString("Exception during file save: %1. Path: %2").arg(e.what()).arg(savePath);
        }
        catch (...)
        {
            errorMsg = QString("Unknown exception during file save. Path: %1").arg(savePath);
        }

        if (!success)
        {
            log_write_error_information("Save task (ID: " + to_string(taskId) + ") failed. " + errorMsg.toStdString());
        }
        else
        {
            log_write_regular_information("Save task (ID: " + to_string(taskId) + ") successful. Saved to: " + savePath.toStdString());
        }
        QMetaObject::invokeMethod(this, "handleFileReceptionResult", Qt::QueuedConnection,
                                  Q_ARG(QString, qOriginalFileName),
                                  Q_ARG(QString, savePath),
                                  Q_ARG(bool, success),
                                  Q_ARG(QString, errorMsg));
        removeTaskFuture(taskId);
    });
    {
        lock_guard lock(taskMapMutex_);
        activeTasks_[taskId] = std::move(saveFuture);
        log_write_regular_information("Stored future for save task ID: " + to_string(taskId));
    }
}

QListWidgetItem *MainWindow::findTaskItemById(qint64 taskId)
{
    for (int i = 0; i < taskListWidget->count(); ++i)
    {
        QListWidgetItem *currentItem = taskListWidget->item(i);
        QVariant idData = currentItem->data(TaskIdRole);
        if (idData.isValid() && idData.toLongLong() == taskId)
            return currentItem;
    }
    return nullptr;
}

void MainWindow::removeTaskFuture(qint64 taskId)
{
    lock_guard lock(taskMapMutex_);
    if (activeTasks_.erase(taskId) > 0)
        log_write_regular_information("Removed future for completed/failed task ID: " + to_string(taskId));
    else
        log_write_warning_information("Attempted to remove future for task ID: " + to_string(taskId) + ", but it was not found in the map.");
}

int runMainWindow(ClientSocket &clientSocket, const vector<string> &parameters)
{
    int argc = 1;
    char *argv[] = {(char *)"KleeExplorerApp", nullptr};
    QApplication *app_ptr = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app_ptr)
    {
        char **non_const_argv = const_cast<char **>(argv);
        app_ptr = new QApplication(argc, non_const_argv);
    }
    QApplication &app = *app_ptr;
    MainWindow mainWindow(clientSocket, nullptr);

    clientSocket.register_handler("cpl-ret",                            
                                  [&mainWindow](const string &payload) {
                                      log_write_regular_information("Received 'cpl-ret' message.");
                                      size_t null_pos = payload.find('\0');
                                      if (null_pos != string::npos)
                                      {
                                          string filename = payload.substr(0, null_pos);
                                          vector<char> fileData(payload.begin() + null_pos + 1, payload.end());
                                          mainWindow.handleReceivedFileDataAsync(filename, fileData); 
                                      }
                                      else
                                      {
                                          log_write_error_information("Invalid 'cpl-ret' payload format received.");
                                      }
                                  });

    mainWindow.show();
    log_write_regular_information("MainWindow shown. Starting event loop...");
    int exitCode = app.exec();
    log_write_regular_information("Qt event loop finished with exit code: " + to_string(exitCode));
    return exitCode;
}

#include "MainWindow.moc"
