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
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include <QApplication>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QSplitter>
#include <QToolBar>
#include <QTreeView>
#include <QListView>
#include <QLineEdit>
#include <QListWidget>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>
#include <QMutex>
#include <QDataStream>
#include <QtEndian>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDebug>

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <tuple>

enum class UITaskStatus : quint8
{
    Preparing,
    InProgress,
    AwaitingServer,
    Completed,
    Error
};

const int UITaskStatusRole = Qt::UserRole + 1;
const int FilePathRole = Qt::UserRole + 2;
const int OriginalFileNameRole = Qt::UserRole + 3;
const int ErrorMessageRole = Qt::UserRole + 4;

class TaskManager : public QObject
{
    Q_OBJECT
public:
    explicit TaskManager(ClientSocket &sock, const QString &outDir, QObject *parent = nullptr)
        : QObject(parent), socket_(sock), outputDir_(outDir)
    {
        if (!QDir(outputDir_).exists() && !QDir().mkpath(outputDir_))
        {
            log_write_error_information("TaskManager: Failed to create output directory: " + outputDir_.toStdString());
        }
    }

    void initiateSendFile(const QString &absoluteFilePath)
    {
        log_write_regular_information("TaskManager: Queuing send initiation for: " + absoluteFilePath.toStdString());
        auto *watcher = new QFutureWatcher<std::tuple<QString, bool, QString>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]()
                {
            auto result = watcher->result();
            emit sendFileInitiationCompleted(std::get<0>(result), std::get<1>(result), std::get<2>(result));
            watcher->deleteLater(); });
        watcher->setFuture(QtConcurrent::run(
            &TaskManager::workerSendFileInitiation, &socket_, absoluteFilePath));
    }

    void saveReceivedFile(const QString &originalFileName, const std::vector<char> &fileData)
    {
        log_write_regular_information("TaskManager: Queuing save operation for received file (original name): " + originalFileName.toStdString());
        auto *watcher = new QFutureWatcher<std::tuple<QString, QString, bool, QString>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]()
                {
            auto result = watcher->result();
            emit receivedFileSaveCompleted(std::get<0>(result), std::get<1>(result), std::get<2>(result), std::get<3>(result));
            watcher->deleteLater(); });
        watcher->setFuture(QtConcurrent::run(
            &TaskManager::workerSaveReceivedFile, originalFileName, fileData, outputDir_));
    }

signals:
    void sendFileInitiationCompleted(QString filePath, bool success, QString errorReason);
    void receivedFileSaveCompleted(QString originalFileName, QString savedFilePath, bool success, QString errorReason);

private:
    static std::tuple<QString, bool, QString> workerSendFileInitiation(ClientSocket *socket, QString filePath)
    {
        log_write_regular_information("[Worker] TaskManager: Initiating send for: " + filePath.toStdString());
        QString errorMsg;
        bool success = false;
        bool connectionReady = false;

        if (socket->is_connected())
        {
            connectionReady = true;
        }
        else
        {
            log_write_warning_information("[Worker] TaskManager: Socket not connected for " + filePath.toStdString() + ". Attempting to connect...");
            if (socket->connect())
            {
                if (socket->is_connected())
                {
                    connectionReady = true;
                    log_write_regular_information("[Worker] TaskManager: Socket connected successfully for " + filePath.toStdString());
                }
                else
                {
                    errorMsg = "Failed to establish connection after connect() call.";
                    log_write_error_information("[Worker] TaskManager: " + errorMsg.toStdString() + " for " + filePath.toStdString());
                }
            }
            else
            {
                errorMsg = "socket->connect() returned false.";
                log_write_error_information("[Worker] TaskManager: " + errorMsg.toStdString() + " for " + filePath.toStdString());
            }
        }

        if (connectionReady)
        {
            try
            {
                if (socket->send_file("compile-execute", filePath.toStdString()))
                {
                    success = true;
                    log_write_regular_information("[Worker] TaskManager: send_file call successful for " + filePath.toStdString() + ". Awaiting server processing.");
                }
                else
                {
                    errorMsg = "ClientSocket::send_file returned false.";
                    log_write_error_information("[Worker] TaskManager: " + errorMsg.toStdString() + " for " + filePath.toStdString());
                }
            }
            catch (const std::exception &e)
            {
                errorMsg = QString("Exception during send_file: %1").arg(e.what());
                log_write_error_information("[Worker] TaskManager: " + errorMsg.toStdString() + " for " + filePath.toStdString());
            }
            catch (...)
            {
                errorMsg = "Unknown exception during send_file.";
                log_write_error_information("[Worker] TaskManager: " + errorMsg.toStdString() + " for " + filePath.toStdString());
            }
        }
        else
        {
            if (errorMsg.isEmpty())
                errorMsg = "Connection not ready and could not be established.";
            log_write_error_information("[Worker] TaskManager: Cannot send " + filePath.toStdString() + " due to connection issue: " + errorMsg.toStdString());
        }
        return {filePath, success, errorMsg};
    }

    static std::tuple<QString, QString, bool, QString> workerSaveReceivedFile(
        QString originalFileName, const std::vector<char> &fileDataVec, QString outputDir)
    {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
        QString baseName = QFileInfo(originalFileName).completeBaseName();
        if (baseName.isEmpty())
            baseName = "received_file";
        QString suffix = QFileInfo(originalFileName).suffix();
        QString savedFileName = QString("%1_%2%3%4").arg(baseName, timestamp, (suffix.isEmpty() ? "" : "."), suffix);
        QString savePath = QDir(outputDir).filePath(savedFileName);

        log_write_regular_information(("[Worker] TaskManager: Attempting to save '" + originalFileName + "' as '" + savePath + "'").toStdString());
        bool success = false;
        QString errorMsg;
        QFile file(savePath);

        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            errorMsg = QString("Failed to open file for writing: %1. Error: %2").arg(savePath, file.errorString());
        }
        else
        {
            qint64 bytesWritten = file.write(fileDataVec.data(), static_cast<qint64>(fileDataVec.size()));
            bool closedSuccessfully = file.close();

            if (bytesWritten != static_cast<qint64>(fileDataVec.size()))
            {
                errorMsg = QString("Failed to write all data to file: %1. Written: %2, Expected: %3. File error: %4")
                               .arg(savePath)
                               .arg(bytesWritten)
                               .arg(fileDataVec.size())
                               .arg(file.errorString());
            }
            else if (!closedSuccessfully)
            {
                errorMsg = QString("Failed to close file after writing: %1. File error: %2").arg(savePath, file.errorString());
            }
            else
            {
                if (file.error() != QFileDevice::NoError)
                {
                    errorMsg = QString("File operation reported error after successful write/close: %1. Error: %2")
                                   .arg(savePath, file.errorString());
                }
                else
                {
                    success = true;
                }
            }
            if (!success && QFile::exists(savePath))
            {
                log_write_warning_information(("[Worker] TaskManager: Attempting to remove partially written or problematic file: '" + savePath + "'").toStdString());
                if (!QFile::remove(savePath))
                {
                    log_write_error_information(("[Worker] TaskManager: Failed to remove problematic file: '" + savePath + "'").toStdString());
                }
            }
        }

        if (!success)
            log_write_error_information(("[Worker] TaskManager: Save failed for '" + originalFileName + "'. Reason: " + errorMsg).toStdString());
        else
            log_write_regular_information(("[Worker] TaskManager: Successfully saved '" + originalFileName + "' as '" + savePath + "'").toStdString());

        return {originalFileName, savePath, success, errorMsg};
    }

    ClientSocket &socket_;
    QString outputDir_;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ClientSocket &sock, QWidget *parent = nullptr)
        : QMainWindow(parent), clientSocketInstance_(sock), taskManager_(sock, outputDirectory, this)
    {
        setupUi();
        connectSignalsAndSlots();
        log_write_regular_information("MainWindow initialized and UI built.");
        startNavigateToPath(QDir::homePath());
    }

    ~MainWindow() override
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

private slots:
    void onTreeViewClicked(const QModelIndex &index)
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

    void onListViewDoubleClicked(const QModelIndex &index)
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

    void onPathLineEditReturnPressed()
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

    void onGoUpActionTriggered()
    {
        QString currentPath = fsListModel_->rootPath();
        if (currentPath.isEmpty() || QDir(currentPath).isRoot())
        {
            log_write_regular_information("Cannot go up, already at root or path is invalid.");
            QMessageBox::information(this, "提示", "已经是根目录啦！");
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

    void onListViewSelectionChanged(const QItemSelection & /*selected*/, const QItemSelection & /*deselected*/)
    {
        const QModelIndexList indexes = fsList_->selectionModel()->selectedIndexes();
        bool canSend = (indexes.count() == 1 && fsListModel_->fileInfo(indexes.first()).isFile());
        sendAction_->setEnabled(canSend);
    }

    void onSendActionTriggered()
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
                QMessageBox::warning(this, "任务已存在", QString("文件 '%1' 的发送任务已经在进行中或等待服务器响应。").arg(fileName));
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

    void onTaskListItemDoubleClicked(QListWidgetItem *item)
    {
        if (!item)
            return;
        UITaskStatus status = static_cast<UITaskStatus>(item->data(UITaskStatusRole).value<quint8>());
        QString filePath = item->data(FilePathRole).toString();
        QString originalFileName = item->data(OriginalFileNameRole).toString();

        log_write_regular_information(QString("Task item double-clicked. OriginalFileName: %1, Status: %2")
                                          .arg(originalFileName)
                                          .arg(static_cast<int>(status))
                                          .toStdString());

        if (status == UITaskStatus::Completed)
        {
            if (filePath.isEmpty())
            {
                log_write_warning_information("Completed task item has no file path.");
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)))
            {
                QMessageBox::warning(this, "无法打开", "无法使用默认程序打开文件:\n" + filePath);
            }
        }
        else if (status == UITaskStatus::Error)
        {
            QString errorMsg = item->data(ErrorMessageRole).toString();
            QMessageBox::warning(this, "任务错误", "此任务执行失败:\n" + (errorMsg.isEmpty() ? "未提供具体信息。" : errorMsg));
        }
        else
        {
            QMessageBox::information(this, "任务进行中", QString("任务 '%1' 仍在进行中...").arg(originalFileName));
        }
    }

    void onClearTasksButtonClicked()
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
                        if (activeSendTaskItems_.count(originalFileName) && activeSendTaskItems_[originalFileName] == item)
                        {
                            log_write_warning_information("Found a supposedly completed/error task still in activeSendTaskItems_: " + originalFileName.toStdString() + ". Removing.");
                            activeSendTaskItems_.erase(originalFileName);
                        }
                    }
                }
            }
        }
        log_write_regular_information("Finished clearing completed/error tasks.");
    }

    void onSendFileInitiationCompleted(QString filePath, bool success, QString errorReason)
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

    void onReceivedFileSaveCompleted(QString originalFileName, QString savedFilePath, bool success, QString errorReason)
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

    void handleNavigationFinished()
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
                log_write_warning_information("ListViewModel setRootPath effective path mismatch for: " + targetPath.toStdString());
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
            upAction_->setEnabled(!QDir(targetPath).isRoot() && QDir(targetPath).cdUp());
            fsList_->clearSelection();
            sendAction_->setEnabled(false);
        }
        else
        {
            QMessageBox::warning(this, "路径无效", QString("无法导航到路径：%1\n%2").arg(targetPath, errorMsg));
            pathEdit_->setText(QDir::toNativeSeparators(fsListModel_->rootPath()));
        }
    }

    void handleOpenFileFinished()
    {
        if (!openFileWatcher_.isFinished())
            return;
        QString filePath = openFileWatcher_.property("filePath").toString();
        bool success = openFileWatcher_.result();
        if (!success)
        {
            log_write_error_information("Failed to open file: " + filePath.toStdString());
            QMessageBox::warning(this, "无法打开文件", "无法使用关联程序打开:\n" + filePath);
        }
        else
        {
            log_write_regular_information("Successfully requested to open file: " + filePath.toStdString());
        }
    }

private:
    void setupUi()
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

        fsList_ = new QListView(this);
        fsList_->setModel(fsListModel_);
        fsList_->setViewMode(QListView::IconMode);
        fsList_->setGridSize(QSize(90, 90));
        fsList_->setResizeMode(QListView::Adjust);
        fsList_->setSelectionMode(QAbstractItemView::SingleSelection);
        fsList_->setUniformItemSizes(true);

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
        mainSplitter->setStretchFactor(1, 2);

        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->addWidget(pathEdit_);
        mainLayout->addWidget(mainSplitter);
        setCentralWidget(centralWidget);

        setWindowTitle(QString("Simple-K File Explorer v6.0 - Logger & Main Refactor!"));
        resize(1200, 850);
    }

    void connectSignalsAndSlots()
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

        clientSocketInstance_.register_handler("compile-execute",
                                               [this](const std::string &payload_str)
                                               {
                                                   log_write_regular_information("Socket Handler: Received 'compile-execute' message from server. Length: " + std::to_string(payload_str.length()));

                                                   QByteArray payload = QByteArray::fromStdString(payload_str);
                                                   QDataStream stream(&payload, QIODevice::ReadOnly);
                                                   stream.setByteOrder(QDataStream::LittleEndian);

                                                   quint32 nameLen;
                                                   if (stream.device()->bytesAvailable() < sizeof(quint32))
                                                   {
                                                       log_write_error_information("Socket Handler: Payload too short for nameLen.");
                                                       return;
                                                   }
                                                   stream >> nameLen;

                                                   if (stream.device()->bytesAvailable() < nameLen)
                                                   {
                                                       log_write_error_information("Socket Handler: Payload too short for name string.");
                                                       return;
                                                   }
                                                   QByteArray nameBytes(nameLen, Qt::Uninitialized);
                                                   stream.readRawData(nameBytes.data(), nameLen);
                                                   QString originalFileName = QString::fromUtf8(nameBytes);

                                                   quint8 successFlagVal;
                                                   if (stream.device()->bytesAvailable() < sizeof(quint8))
                                                   {
                                                       log_write_error_information("Socket Handler: Payload too short for successFlag.");
                                                       return;
                                                   }
                                                   stream >> successFlagVal;
                                                   bool serverProcessingSuccess = (successFlagVal != 0);

                                                   quint32 msgLen;
                                                   if (stream.device()->bytesAvailable() < sizeof(quint32))
                                                   {
                                                       log_write_error_information("Socket Handler: Payload too short for msgLen.");
                                                       return;
                                                   }
                                                   stream >> msgLen;

                                                   if (stream.device()->bytesAvailable() < msgLen)
                                                   {
                                                       log_write_error_information("Socket Handler: Payload too short for srvMsg string.");
                                                       return;
                                                   }
                                                   QByteArray msgBytes(msgLen, Qt::Uninitialized);
                                                   stream.readRawData(msgBytes.data(), msgLen);
                                                   QString serverMessage = QString::fromUtf8(msgBytes);

                                                   std::vector<char> fileDataVec;
                                                   if (!stream.atEnd())
                                                   {
                                                       QByteArray remainingData = payload.mid(stream.device()->pos());
                                                       fileDataVec.assign(remainingData.constData(), remainingData.constData() + remainingData.size());
                                                   }

                                                   log_write_regular_information(QString("Socket Handler: Parsed: origName='%1', success=%2, serverMsg='%3', dataSize=%4")
                                                                                     .arg(originalFileName)
                                                                                     .arg(serverProcessingSuccess)
                                                                                     .arg(serverMessage)
                                                                                     .arg(fileDataVec.size())
                                                                                     .toStdString());

                                                   QMetaObject::invokeMethod(this, [this, originalFileName, fileDataVec, serverProcessingSuccess, serverMessage]()
                                                                             { handleServerFileResponse(originalFileName.toStdString(), fileDataVec, serverProcessingSuccess, serverMessage.toStdString()); }, Qt::QueuedConnection);
                                               });
        log_write_regular_information("Registered 'compile-execute' handler with ClientSocket.");
    }

    void handleServerFileResponse(const std::string &originalFileNameFromServer_std, const std::vector<char> &fileData, bool serverProcessingSuccess, const std::string &serverMessage_std)
    {
        QString originalFileNameFromServer = QString::fromStdString(originalFileNameFromServer_std);
        QString serverMessage = QString::fromStdString(serverMessage_std);

        log_write_regular_information(QString("MainWindow::handleServerFileResponse (from socket) for '%1', server success: %2")
                                          .arg(originalFileNameFromServer)
                                          .arg(serverProcessingSuccess)
                                          .toStdString());

        QListWidgetItem *sendingItem = nullptr;
        {
            QMutexLocker locker(&activeSendTasksMutex_);
            auto it = activeSendTaskItems_.find(originalFileNameFromServer);
            if (it != activeSendTaskItems_.end())
            {
                sendingItem = it->second;
                activeSendTaskItems_.erase(it);
                log_write_regular_information("Removed " + originalFileNameFromServer.toStdString() + " from activeSendTaskItems_ after server response.");
            }
        }

        if (sendingItem)
        {
            QString sendingItemOriginalPath = sendingItem->data(FilePathRole).toString();
            if (serverProcessingSuccess)
            {
                updateUITaskItem(sendingItem, UITaskStatus::Completed,
                                 QString("Processed by server: %1. %2").arg(originalFileNameFromServer, serverMessage),
                                 QString("Original: %1. Server: %2").arg(sendingItemOriginalPath, serverMessage));
            }
            else
            {
                updateUITaskItem(sendingItem, UITaskStatus::Error,
                                 QString("Server error for %1: %2").arg(originalFileNameFromServer, serverMessage),
                                 QString("Original: %1. Server Error: %2").arg(sendingItemOriginalPath, serverMessage),
                                 serverMessage);
            }
        }
        else
        {
            log_write_warning_information("Could not find task item in activeSendTaskItems_ for server response: " + originalFileNameFromServer.toStdString());
        }

        if (serverProcessingSuccess && !fileData.empty())
        {
            log_write_regular_information("Server processing was successful and file data received for " + originalFileNameFromServer.toStdString() + ". Requesting TaskManager to save.");
            taskManager_.saveReceivedFile(originalFileNameFromServer, fileData);
        }
        else if (!fileData.empty())
        {
            log_write_warning_information("Server processing failed for " + originalFileNameFromServer.toStdString() + " but file data was present. Data will not be saved.");
        }
    }

    void startNavigateToPath(const QString &path)
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

    static std::pair<bool, QString> performNavigationTask(QString path)
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
        return {true, QString()};
    }

    void startOpenFile(const QString &filePath)
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

    void updateUITaskItem(QListWidgetItem *item, UITaskStatus status, const QString &displayText, const QString &toolTipText = QString(), const QString &errorMsgForRole = QString())
    {
        if (!item)
        {
            log_write_error_information("updateUITaskItem: item is null!");
            return;
        }

        QString statusPrefix;
        QColor textColor = palette().color(QPalette::WindowText);

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

    QListWidgetItem *findActiveSendingTaskByOriginalName(const QString &originalFileName)
    {
        QMutexLocker locker(&activeSendTasksMutex_);
        auto it = activeSendTaskItems_.find(originalFileName);
        if (it != activeSendTaskItems_.end())
        {
            return it->second;
        }
        return nullptr;
    }

private:
    QFileSystemModel *fsTreeModel_ = nullptr;
    QFileSystemModel *fsListModel_ = nullptr;
    QTreeView *fsTree_ = nullptr;
    QListView *fsList_ = nullptr;
    QLineEdit *pathEdit_ = nullptr;
    QListWidget *taskListWidget_ = nullptr;
    QAction *upAction_ = nullptr;
    QAction *sendAction_ = nullptr;

    ClientSocket &clientSocketInstance_;
    TaskManager taskManager_;

    std::map<QString, QListWidgetItem *> activeSendTaskItems_;
    QMutex activeSendTasksMutex_;

    QFutureWatcher<std::pair<bool, QString>> navigationWatcher_;
    QFutureWatcher<bool> openFileWatcher_;

    const QString outputDirectory = "out";
};

int runMainWindow(ClientSocket &client, const std::vector<std::string> &args)
{

    log_write_regular_information("runMainWindow called. Args count: " + std::to_string(args.size()));
    for (size_t i = 0; i < args.size(); ++i)
    {
        log_write_regular_information("Arg " + std::to_string(i) + ": " + args[i]);
    }

    int argc = 1;
    char appName[] = "SimpleKFileExplorer";
    char *argv[] = {appName, nullptr};

    QApplication *app_ptr = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app_ptr)
    {
        app_ptr = new QApplication(argc, argv);
        log_write_regular_information("New QApplication instance created in runMainWindow.");
    }
    else
    {
        log_write_regular_information("Using existing QApplication instance in runMainWindow.");
    }
    QApplication &app = *app_ptr;
    app.setApplicationName("Simple-K File Explorer");
    app.setOrganizationName("KleeWorks");

    MainWindow mainWindow(client);
    mainWindow.show();

    log_write_regular_information("MainWindow shown in runMainWindow. Starting Qt event loop...");
    int exitCode = app.exec();
    log_write_regular_information(QString("Qt event loop finished in runMainWindow with exit code: %1.").arg(exitCode).toStdString());
    return exitCode;
}

const char *SERVER_IP = "127.0.0.1";
uint16_t DEFAULT_PORT = 3040;

int main(int argc, char *argv[])
{

    log_write_regular_information("Application main starting...");

    ClientSocket client(SERVER_IP, DEFAULT_PORT);
    client.register_default_handler([](const std::string &payload)
                                    { log_write_warning_information("(client default handler) message received but no tag met: " + payload); });

    client.register_handler("Hello", [](const std::string &payload)
                            { log_write_regular_information("Client received Hello from server: " + payload); });
    client.register_handler("error-information", [](const std::string &payload)
                            { log_write_error_information("Client received error-information from server: " + payload); });

    if (client.connect())
    {
        log_write_regular_information("Client connected to server in main. Sending initial 'Hello'.");
        if (!client.send_message("Hello", "Hello from Klee's File Explorer client!"))
        {
            log_write_error_information("Failed to send initial 'Hello' message to server.");
        }
    }
    else
    {
        log_write_error_information("Failed to connect to server in main. GUI will start but network features might fail.");
    }

    std::vector<std::string> args_vec;
    for (int i = 1; i < argc; ++i)
    {
        args_vec.push_back(argv[i]);
    }

    return runMainWindow(client, args_vec);
}

#include "MainWindow.moc"
