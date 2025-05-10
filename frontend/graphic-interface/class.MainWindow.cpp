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

#define _CLASS_MAINWINDOW_CPP
#include "graphic-interface.hpp"

namespace
{
    struct ParsedPayload
    {
        QString originalFileName;
        std::vector<char> fileData;
        bool successfullyParsed = false;
        QString message;

        ParsedPayload() = default;
    };

    ParsedPayload parseEchoPayload(const std::string &payload_str)
    {
        log_write_regular_information("[MainWindow::AnonParser] Attempting to parse echo payload. Raw Length: " + std::to_string(payload_str.length()));

        ParsedPayload result;

        size_t nullTerminatorPos = payload_str.find('\0');

        if (nullTerminatorPos == std::string::npos)
        {
            result.successfullyParsed = false;
            result.message = "Error: Malformed payload (filename null terminator not found).";
            return result;
        }

        std::string fileNameStdStr = payload_str.substr(0, nullTerminatorPos);
        result.originalFileName = QString::fromStdString(fileNameStdStr);

        if (payload_str.length() > nullTerminatorPos + 1)
        {
            const char *fileContentStart = payload_str.data() + nullTerminatorPos + 1;
            size_t fileContentLength = payload_str.length() - (nullTerminatorPos + 1);
            result.fileData.assign(fileContentStart, fileContentStart + fileContentLength);
        }

        result.successfullyParsed = true;
        if (result.fileData.empty() && payload_str.length() == nullTerminatorPos + 1)
        {
            result.message = QString("Successfully parsed echo response for '%1'; no file data returned by server.").arg(result.originalFileName);
        }
        else
        {
            result.message = QString("Successfully parsed echo response for '%1'; file data received from server.").arg(result.originalFileName);
        }

        return result;
    }

}

MainWindow::MainWindow(ClientSocket &sock, QWidget *parent)
    : QMainWindow(parent),
      clientSocketInstance_(sock),
      taskManager_(clientSocketInstance_, outputDirectory, this)
{
    setupUi();
    connectSignalsAndSlots();
    log_write_regular_information("MainWindow initialized and UI built.");
    startNavigateToPath(QDir::homePath());
}

MainWindow::~MainWindow()
{
    log_write_regular_information("MainWindow destructor called.");
    if (navigationWatcher_.isRunning())
    {
        log_write_regular_information("Cancelling active navigation task.");
        navigationWatcher_.cancel();
        navigationWatcher_.waitForFinished();
    }
    if (openFileWatcher_.isRunning())
    {
        log_write_regular_information("Cancelling active open file task.");
        openFileWatcher_.cancel();
        openFileWatcher_.waitForFinished();
    }
}

void MainWindow::setupUi()
{

    fsTreeModel_ = new QFileSystemModel(this);
    fsTreeModel_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden);
    fsTreeModel_->setRootPath("");

    fsListModel_ = new QFileSystemModel(this);
    fsListModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    fsTree_ = new QTreeView(this);
    fsTree_->setModel(fsTreeModel_);
    fsTree_->setRootIndex(fsTreeModel_->index(QDir::rootPath()));
    for (int i = 1; i < fsTreeModel_->columnCount(); ++i)
        fsTree_->hideColumn(i);
    fsTree_->setSortingEnabled(true);
    fsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    fsList_ = new QListView(this);
    fsList_->setModel(fsListModel_);
    fsList_->setViewMode(QListView::IconMode);
    fsList_->setGridSize(QSize(100, 100));
    fsList_->setResizeMode(QListView::Adjust);
    fsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    fsList_->setUniformItemSizes(true);
    fsList_->setWordWrap(true);

    pathEdit_ = new QLineEdit(this);
    pathEdit_->setPlaceholderText("Enter path and press Enter");

    QToolBar *mainToolBar = addToolBar("Main Toolbar");
    upAction_ = mainToolBar->addAction(QIcon::fromTheme("go-up", QIcon(":/qt-project.org/styles/commonstyle/images/uparr-16.png")), "Go Up");
    sendAction_ = mainToolBar->addAction(QIcon::fromTheme("document-send", QIcon(":/qt-project.org/styles/commonstyle/images/network-transmit-16.png")), "Send File");
    sendAction_->setEnabled(false);

    taskListWidget_ = new QListWidget(this);
    taskListWidget_->setAlternatingRowColors(true);
    QPushButton *clearTasksButton = new QPushButton("Clear Finished/Error Tasks", this);

    QWidget *taskAreaWidget = new QWidget(this);
    QVBoxLayout *taskLayout = new QVBoxLayout(taskAreaWidget);
    taskLayout->addWidget(new QLabel("Network Tasks:", this));
    taskLayout->addWidget(taskListWidget_);
    QHBoxLayout *taskButtonLayout = new QHBoxLayout();
    taskButtonLayout->addStretch();
    taskButtonLayout->addWidget(clearTasksButton);
    taskLayout->addLayout(taskButtonLayout);

    QSplitter *rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(fsList_);
    rightSplitter->addWidget(taskAreaWidget);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);

    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(fsTree_);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addWidget(pathEdit_);
    mainLayout->addWidget(mainSplitter);
    setCentralWidget(centralWidget);

    setWindowTitle(QString("Simple-K File Explorer & Cloud Executor - v%1").arg(APP_VERSION));
    resize(1200, 850);

    connect(clearTasksButton, &QPushButton::clicked, this, &MainWindow::onClearTasksButtonClicked);
}

void MainWindow::connectSignalsAndSlots()
{

    connect(fsTree_, &QTreeView::clicked, this, &MainWindow::onTreeViewClicked);
    connect(fsList_, &QListView::doubleClicked, this, &MainWindow::onListViewDoubleClicked);
    connect(pathEdit_, &QLineEdit::returnPressed, this, &MainWindow::onPathLineEditReturnPressed);
    connect(upAction_, &QAction::triggered, this, &MainWindow::onGoUpActionTriggered);

    connect(sendAction_, &QAction::triggered, this, &MainWindow::onSendActionTriggered);
    connect(fsList_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onListViewSelectionChanged);

    connect(taskListWidget_, &QListWidget::itemDoubleClicked, this, &MainWindow::onTaskListItemDoubleClicked);

    connect(&taskManager_, &TaskManager::sendFileInitiationCompleted, this, &MainWindow::onSendFileInitiationCompleted);
    connect(&taskManager_, &TaskManager::receivedFileSaveCompleted, this, &MainWindow::onReceivedFileSaveCompleted);

    connect(&navigationWatcher_, &QFutureWatcherBase::finished, this, &MainWindow::handleNavigationFinished);
    connect(&openFileWatcher_, &QFutureWatcherBase::finished, this, &MainWindow::handleOpenFileFinished);

    clientSocketInstance_.register_handler(
        "compile-execute",
        [this](const std::string &payload_str)
        {
            log_write_regular_information("Socket Handler: Received 'compile-execute' message. Raw Length: " + std::to_string(payload_str.length()));

            ParsedPayload parsedResponse = parseEchoPayload(payload_str);

            if (parsedResponse.successfullyParsed)
            {
                log_write_regular_information(QString("Socket Handler: Payload parsed successfully. FileName: '%1', DataSize: %2, Message: '%3'")
                                                  .arg(parsedResponse.originalFileName)
                                                  .arg(parsedResponse.fileData.size())
                                                  .arg(parsedResponse.message)
                                                  .toStdString());
            }
            else
            {
                log_write_error_information(QString("Socket Handler: Failed to parse payload. Error: '%1'")
                                                .arg(parsedResponse.message)
                                                .toStdString());
            }

            QMetaObject::invokeMethod(this, [this,

                                             fileName = parsedResponse.originalFileName, data = parsedResponse.fileData, isParsedSuccessfully = parsedResponse.successfullyParsed, messageFromServer = parsedResponse.message]()
                                      { handleServerFileResponse(
                                            fileName.toStdString(),
                                            data,
                                            isParsedSuccessfully,
                                            messageFromServer.toStdString()); },
                                      Qt::QueuedConnection);
        });
    log_write_regular_information("Registered 'compile-execute' handler with ClientSocket.");
}

void MainWindow::onTreeViewClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QString path = fsTreeModel_->filePath(index);
    if (path.isEmpty())
    {
        log_write_warning_information("Empty path from tree view click.");
        return;
    }
    log_write_regular_information("Tree view clicked, navigating to: " + path.toStdString());
    startNavigateToPath(path);
}

void MainWindow::onListViewDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QFileInfo fileInfo = fsListModel_->fileInfo(index);
    if (fileInfo.isDir())
    {
        log_write_regular_information("List view directory double-clicked: " + fileInfo.absoluteFilePath().toStdString());
        startNavigateToPath(fileInfo.absoluteFilePath());
    }
    else if (fileInfo.isFile())
    {
        log_write_regular_information("List view file double-clicked: " + fileInfo.absoluteFilePath().toStdString());
        startOpenFile(fileInfo.absoluteFilePath());
    }
}

void MainWindow::onPathLineEditReturnPressed()
{
    QString path = pathEdit_->text();
    if (path.isEmpty())
    {
        log_write_warning_information("Path line edit empty, navigation aborted.");
        return;
    }
    log_write_regular_information("Path line edit return pressed, navigating to: " + path.toStdString());
    startNavigateToPath(path);
}

void MainWindow::onGoUpActionTriggered()
{
    QString currentPath = fsListModel_->rootPath();
    if (currentPath.isEmpty() || QDir(currentPath).isRoot())
    {
        log_write_regular_information("Cannot go up, already at root or path is invalid.");
        QMessageBox::information(this, "提示", "已经是根目录啦！(Already at root directory!)");
        return;
    }
    QDir dir(currentPath);
    if (dir.cdUp())
    {
        log_write_regular_information("Navigating up to: " + dir.absolutePath().toStdString());
        startNavigateToPath(dir.absolutePath());
    }
    else
    {
        log_write_warning_information("Failed to navigate up from: " + currentPath.toStdString());
    }
}

void MainWindow::onListViewSelectionChanged(const QItemSelection & /*selected*/, const QItemSelection & /*deselected*/)
{
    const QModelIndexList indexes = fsList_->selectionModel()->selectedIndexes();
    bool canSend = (indexes.count() == 1 && fsListModel_->fileInfo(indexes.first()).isFile());
    sendAction_->setEnabled(canSend);
}

void MainWindow::onSendActionTriggered()
{
    const QModelIndexList indexes = fsList_->selectionModel()->selectedIndexes();
    if (indexes.isEmpty())
        return;
    QFileInfo fileInfo = fsListModel_->fileInfo(indexes.first());
    if (!fileInfo.isFile())
        return;

    QString filePath = fileInfo.absoluteFilePath();
    QString fileName = fileInfo.fileName();
    log_write_regular_information("Send action triggered for: " + filePath.toStdString());

    {
        QMutexLocker locker(&activeSendTasksMutex_);
        if (activeSendTaskItems_.count(fileName))
        {
            QMessageBox::warning(this, "任务已存在 (Task Already Exists)", QString("文件 '%1' 的发送任务已经在进行中或等待服务器响应。(Task for file '%1' is already in progress or awaiting server response.)").arg(fileName));
            log_write_warning_information("Send request for " + fileName.toStdString() + " aborted, task already exists in activeSendTaskItems_.");
            return;
        }
    }

    QListWidgetItem *newItem = new QListWidgetItem();
    newItem->setData(OriginalFileNameRole, fileName);
    newItem->setData(FilePathRole, filePath);
    taskListWidget_->addItem(newItem);
    updateUITaskItem(newItem, UITaskStatus::Preparing, QString("Preparing to send: %1").arg(fileName));

    {
        QMutexLocker locker(&activeSendTasksMutex_);
        activeSendTaskItems_[fileName] = newItem;
    }
    taskManager_.initiateSendFile(filePath);
}

void MainWindow::onTaskListItemDoubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    UITaskStatus status = static_cast<UITaskStatus>(item->data(UITaskStatusRole).value<quint8>());
    QString filePath = item->data(FilePathRole).toString();
    QString originalFileName = item->data(OriginalFileNameRole).toString();

    log_write_regular_information(QString("Task item double-clicked. OriginalFileName: %1, Status: %2, FilePath (local/saved): %3")
                                      .arg(originalFileName)
                                      .arg(static_cast<int>(status))
                                      .arg(filePath)
                                      .toStdString());

    if (status == UITaskStatus::Completed)
    {
        if (filePath.isEmpty())
        {
            log_write_warning_information("Completed task item has no file path associated (FilePathRole).");
            QMessageBox::information(this, "信息 (Information)", QString("任务 '%1' 已完成，但没有关联的本地文件路径可打开。(Task '%1' completed, but no associated local file path to open.)").arg(originalFileName));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)))
        {
            QMessageBox::warning(this, "无法打开 (Cannot Open)", "无法使用默认程序打开文件 (Unable to open file with default application):\n" + filePath);
        }
    }
    else if (status == UITaskStatus::Error)
    {
        QString errorMsg = item->data(ErrorMessageRole).toString();
        QMessageBox::warning(this, "任务错误 (Task Error)", "此任务执行失败 (This task failed):\n" + (errorMsg.isEmpty() ? "未提供具体信息。(No specific information provided.)" : errorMsg));
    }
    else
    {
        QMessageBox::information(this, "任务进行中 (Task in Progress)", QString("任务 '%1' 仍在进行中... (Task '%1' is still in progress...)").arg(originalFileName));
    }
}

void MainWindow::onClearTasksButtonClicked()
{
    log_write_regular_information("Clear tasks button clicked.");
    for (int i = taskListWidget_->count() - 1; i >= 0; --i)
    {
        QListWidgetItem *item = taskListWidget_->item(i);
        if (item)
        {
            UITaskStatus status = static_cast<UITaskStatus>(item->data(UITaskStatusRole).value<quint8>());
            if (status == UITaskStatus::Completed || status == UITaskStatus::Error)
            {
                QString originalFileName = item->data(OriginalFileNameRole).toString();
                log_write_regular_information("Removing task item from list: " + item->text().toStdString());
                delete taskListWidget_->takeItem(i);

                if (!originalFileName.isEmpty())
                {
                    QMutexLocker locker(&activeSendTasksMutex_);

                    auto it = activeSendTaskItems_.find(originalFileName);
                    if (it != activeSendTaskItems_.end() && it->second == item)
                    {
                        log_write_warning_information("Found a completed/error task that was still in activeSendTaskItems_ (should have been removed earlier): " + originalFileName.toStdString() + ". Removing now.");
                        activeSendTaskItems_.erase(it);
                    }
                }
            }
        }
    }
    log_write_regular_information("Finished clearing completed/error tasks.");
}

void MainWindow::onSendFileInitiationCompleted(QString filePath, bool success, QString errorReason)
{
    QString fileName = QFileInfo(filePath).fileName();
    log_write_regular_information(QString("Received sendFileInitiationCompleted for %1. Success: %2. Reason: %3")
                                      .arg(fileName)
                                      .arg(success)
                                      .arg(errorReason)
                                      .toStdString());
    QListWidgetItem *taskItem = nullptr;
    {
        QMutexLocker locker(&activeSendTasksMutex_);
        auto it = activeSendTaskItems_.find(fileName);
        if (it != activeSendTaskItems_.end())
        {
            taskItem = it->second;
        }
    }

    if (!taskItem)
    {
        log_write_error_information("Could not find task item in activeSendTaskItems_ for initiated send: " + fileName.toStdString());
        return;
    }

    if (success)
    {
        updateUITaskItem(taskItem, UITaskStatus::AwaitingServer,
                         QString("Sent: %1 (Awaiting server...)").arg(fileName),
                         QString("File '%1' sent, waiting for server confirmation.").arg(filePath));
    }
    else
    {
        updateUITaskItem(taskItem, UITaskStatus::Error,
                         QString("Send Failed: %1").arg(fileName),
                         QString("Failed to send file '%1'. Reason: %2").arg(filePath, errorReason),
                         errorReason);
        QMutexLocker locker(&activeSendTasksMutex_);
        activeSendTaskItems_.erase(fileName);
        log_write_regular_information("Removed " + fileName.toStdString() + " from activeSendTaskItems_ due to send initiation failure.");
    }
}

void MainWindow::onReceivedFileSaveCompleted(QString originalFileName, QString savedFilePath, bool success, QString errorReason)
{
    log_write_regular_information(QString("Received receivedFileSaveCompleted for original '%1', saved as '%2'. Success: %3. Reason: %4")
                                      .arg(originalFileName)
                                      .arg(savedFilePath)
                                      .arg(success)
                                      .arg(errorReason)
                                      .toStdString());

    QListWidgetItem *newItem = new QListWidgetItem();
    newItem->setData(OriginalFileNameRole, originalFileName);
    newItem->setData(FilePathRole, savedFilePath);
    taskListWidget_->addItem(newItem);

    if (success)
    {
        updateUITaskItem(newItem, UITaskStatus::Completed,
                         QString("Received: %1 -> %2")
                             .arg(originalFileName, QFileInfo(savedFilePath).fileName()),
                         QString("File '%1' received from server and saved as '%2'.")
                             .arg(originalFileName, savedFilePath));
    }
    else
    {
        updateUITaskItem(newItem, UITaskStatus::Error,
                         QString("Save Failed: %1 (originally %2)")
                             .arg(QFileInfo(savedFilePath).fileName().isEmpty() ? originalFileName : QFileInfo(savedFilePath).fileName(), originalFileName),
                         QString("Failed to save received file (original: '%1'). Reason: %2")
                             .arg(originalFileName, errorReason),
                         errorReason);
        QMessageBox::warning(this, "接收保存失败", QString("保存服务器返回的文件 '%1' 失败。\n原因: %2").arg(originalFileName, errorReason));
    }
}

void MainWindow::handleNavigationFinished()
{
    if (!navigationWatcher_.isFinished())
        return;
    QString targetPath = navigationWatcher_.property("targetPath").toString();
    std::pair<bool, QString> result = navigationWatcher_.result();
    bool success = result.first;
    QString errorMsg = result.second;

    log_write_regular_information(QString("Navigation to '%1' finished. Success: %2. Message: '%3'")
                                      .arg(targetPath)
                                      .arg(success)
                                      .arg(errorMsg)
                                      .toStdString());

    if (success)
    {
        QModelIndex listIdx = fsListModel_->setRootPath(targetPath);
        if (!listIdx.isValid() || fsListModel_->filePath(listIdx) != QDir(targetPath).canonicalPath())
        {
            log_write_warning_information("ListViewModel setRootPath effective path mismatch for: " + targetPath.toStdString() + ". Expected: " + QDir(targetPath).canonicalPath().toStdString() + ", Got: " + fsListModel_->filePath(listIdx).toStdString());
        }
        fsList_->setRootIndex(listIdx);

        QModelIndex treeIdx = fsTreeModel_->index(targetPath);
        if (treeIdx.isValid())
        {
            fsTree_->setCurrentIndex(treeIdx);
            fsTree_->expand(treeIdx);
            fsTree_->scrollTo(treeIdx, QAbstractItemView::EnsureVisible);
        }
        else
        {
            log_write_warning_information("Path not found in tree model after successful navigation: " + targetPath.toStdString());
        }
        pathEdit_->setText(QDir::toNativeSeparators(targetPath));
        QDir currentDir(targetPath);
        upAction_->setEnabled(!currentDir.isRoot());
        fsList_->clearSelection();
        sendAction_->setEnabled(false);
    }
    else
    {
        QMessageBox::warning(this, "路径无效 (Invalid Path)", QString("无法导航到路径 (Cannot navigate to path)：%1\n%2").arg(targetPath, errorMsg));
        pathEdit_->setText(QDir::toNativeSeparators(fsListModel_->rootPath()));
    }
}

void MainWindow::handleOpenFileFinished()
{
    if (!openFileWatcher_.isFinished())
        return;
    QString filePath = openFileWatcher_.property("filePath").toString();
    bool success = openFileWatcher_.result();
    if (!success)
    {
        log_write_error_information("Failed to open file: " + filePath.toStdString());
        QMessageBox::warning(this, "无法打开文件 (Cannot Open File)", "无法使用关联程序打开 (Unable to open with associated application):\n" + filePath);
    }
    else
    {
        log_write_regular_information("Successfully requested to open file: " + filePath.toStdString());
    }
}

void MainWindow::handleServerFileResponse(const std::string &originalFileNameFromServer_std, const std::vector<char> &fileData, bool serverProcessingSuccess, const std::string &serverMessage_std)
{
    QString originalFileNameFromServer = QString::fromStdString(originalFileNameFromServer_std);
    QString serverMessage = QString::fromStdString(serverMessage_std);

    log_write_regular_information(QString("MainWindow::handleServerFileResponse (from socket) for '%1', payload parsed successfully: %2. Parser message: '%3'")
                                      .arg(originalFileNameFromServer)
                                      .arg(serverProcessingSuccess)
                                      .arg(serverMessage)
                                      .toStdString());

    QListWidgetItem *sendingItem = nullptr;
    {
        QMutexLocker locker(&activeSendTasksMutex_);
        auto it = activeSendTaskItems_.find(originalFileNameFromServer);
        if (it != activeSendTaskItems_.end())
        {
            sendingItem = it->second;

            if (!serverProcessingSuccess)
            {

                activeSendTaskItems_.erase(it);
                log_write_warning_information("Removed " + originalFileNameFromServer.toStdString() + " from activeSendTaskItems_ due to payload parsing failure from server.");
            }
            else
            {

                activeSendTaskItems_.erase(it);
                log_write_regular_information("Removed " + originalFileNameFromServer.toStdString() + " from activeSendTaskItems_ after successful payload parsing from server.");
            }
        }
    }

    if (sendingItem)
    {
        QString sendingItemOriginalPath = sendingItem->data(FilePathRole).toString();
        if (serverProcessingSuccess)
        {
            updateUITaskItem(sendingItem, UITaskStatus::Completed,
                             QString("Server Echo: %1. %2").arg(originalFileNameFromServer, serverMessage),
                             QString("Original: %1. Server responded and payload parsed: %2").arg(sendingItemOriginalPath, serverMessage));

            if (!fileData.empty())
            {
                log_write_regular_information("Server echo payload parsed successfully and file data received for " + originalFileNameFromServer.toStdString() + ". Requesting TaskManager to save.");
                taskManager_.saveReceivedFile(originalFileNameFromServer, fileData);
            }
            else
            {
                log_write_regular_information("Server echo payload parsed successfully for " + originalFileNameFromServer.toStdString() + ", but no file data in echo. Nothing to save.");
            }
        }
        else
        {
            updateUITaskItem(sendingItem, UITaskStatus::Error,
                             QString("Payload Error from Server for %1: %2").arg(originalFileNameFromServer, serverMessage),
                             QString("Original: %1. Error parsing server response: %2").arg(sendingItemOriginalPath, serverMessage),
                             serverMessage);
        }
    }
    else
    {

        if (serverProcessingSuccess && !originalFileNameFromServer.isEmpty())
        {
            log_write_warning_information("Received and parsed a server message for '" + originalFileNameFromServer.toStdString() + "' but no matching sending task was found in activeSendTaskItems_.");
            if (!fileData.empty())
            {
                log_write_regular_information("Attempting to save this unexpected (but parsed) file data from server for " + originalFileNameFromServer.toStdString());
                taskManager_.saveReceivedFile(originalFileNameFromServer, fileData);
            }
        }
        else if (!serverProcessingSuccess)
        {
            log_write_error_information("Failed to parse server response and no matching sending task was found for: " + originalFileNameFromServer.toStdString() + ". Parser message: " + serverMessage.toStdString());
        }
    }
}

void MainWindow::startNavigateToPath(const QString &path)
{
    if (path.isEmpty())
    {
        log_write_warning_information("Navigate: Path is empty.");
        return;
    }
    if (navigationWatcher_.isRunning())
    {
        log_write_warning_information("Navigate: Navigation task already running. Request for " + path.toStdString() + " ignored.");

        return;
    }
    log_write_regular_information("Navigate: Starting navigation to: " + path.toStdString());
    navigationWatcher_.setProperty("targetPath", path);
    QFuture<std::pair<bool, QString>> future = QtConcurrent::run(&MainWindow::performNavigationTask, path);
    navigationWatcher_.setFuture(future);
}

std::pair<bool, QString> MainWindow::performNavigationTask(QString path)
{
    log_write_regular_information(("[Worker] Navigate: Validating path '" + path + "'").toStdString());
    QFileInfo pathInfo(path);
    if (!pathInfo.exists())
    {
        return {false, QString("Path does not exist or is inaccessible: %1").arg(path)};
    }
    if (!pathInfo.isDir())
    {
        return {false, QString("Path is not a directory: %1").arg(path)};
    }

    if (!pathInfo.isReadable())
    {
        return {false, QString("Path is not readable: %1").arg(path)};
    }
    return {true, QString()};
}

void MainWindow::startOpenFile(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        log_write_warning_information("OpenFile: File path is empty.");
        return;
    }
    if (openFileWatcher_.isRunning())
    {
        log_write_warning_information("OpenFile: Open file task already running. Request for " + filePath.toStdString() + " ignored.");
        return;
    }
    log_write_regular_information("OpenFile: Starting to open: " + filePath.toStdString());
    openFileWatcher_.setProperty("filePath", filePath);
    QFuture<bool> future = QtConcurrent::run(&QDesktopServices::openUrl, QUrl::fromLocalFile(filePath));
    openFileWatcher_.setFuture(future);
}

void MainWindow::updateUITaskItem(QListWidgetItem *item, UITaskStatus status, const QString &displayText, const QString &toolTipText, const QString &errorMsgForRole)
{
    if (!item)
    {
        log_write_error_information("updateUITaskItem: item is null!");
        return;
    }

    QString statusPrefix;
    QColor textColor = item->listWidget() ? item->listWidget()->palette().color(QPalette::WindowText) : Qt::black;

    switch (status)
    {
    case UITaskStatus::Preparing:
        statusPrefix = "◌ ";
        textColor = Qt::gray;
        break;
    case UITaskStatus::InProgress:
        statusPrefix = "⏳ ";
        textColor = Qt::blue;
        break;
    case UITaskStatus::AwaitingServer:
        statusPrefix = "☁️ ";
        textColor = QColorConstants::Svg::deepskyblue;
        break;
    case UITaskStatus::Completed:
        statusPrefix = "✅ ";
        textColor = QColorConstants::Svg::darkgreen;
        break;
    case UITaskStatus::Error:
        statusPrefix = "❌ ";
        textColor = Qt::red;
        break;
    }
    item->setText(statusPrefix + displayText);
    item->setForeground(textColor);
    item->setData(UITaskStatusRole, static_cast<quint8>(status));
    item->setData(ErrorMessageRole, (status == UITaskStatus::Error) ? (errorMsgForRole.isEmpty() ? displayText : errorMsgForRole) : QVariant());
    item->setToolTip(toolTipText.isEmpty() ? (statusPrefix + displayText) : toolTipText);

    log_write_regular_information(QString("Updated UI task item (Name: %1, Status: %2, Text: %3)")
                                      .arg(item->data(OriginalFileNameRole).toString())
                                      .arg(static_cast<int>(status))
                                      .arg(displayText)
                                      .toStdString());
}

QListWidgetItem *MainWindow::findActiveSendingTaskByOriginalName(const QString &originalFileName)
{
    QMutexLocker locker(&activeSendTasksMutex_);
    auto it = activeSendTaskItems_.find(originalFileName);
    if (it != activeSendTaskItems_.end())
    {
        return it->second;
    }
    return nullptr;
}
