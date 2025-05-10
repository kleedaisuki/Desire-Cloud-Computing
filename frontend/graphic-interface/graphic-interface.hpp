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

#ifndef _BACKEND_GRAPHIC_INTERFACE_HPP
#define _BACKEND_GRAPHIC_INTERFACE_HPP

#include "../network/network.hpp"
#include "../frontend-defs.hpp"
#include "base.ActionTask.hpp"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include <QApplication>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QListWidget>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>
#include <QDataStream>
#include <QtCore/QtEndian>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>
#include <QSplitter>
#include <QIcon>
#include <QTreeView>
#include <QLineEdit>
#include <QMutex>
#include <QVariant>
#include <QHeaderView>
#include <QMutexLocker>
#include <QPointer>

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

inline const int UITaskStatusRole = Qt::UserRole + 1;
inline const int FilePathRole = Qt::UserRole + 2;
inline const int OriginalFileNameRole = Qt::UserRole + 3;
inline const int ErrorMessageRole = Qt::UserRole + 4;

class MainWindow;
class TaskManager : public QObject
{
    Q_OBJECT
public:
    explicit TaskManager(ClientSocket &sock, const QString &outDir, QObject *parent = nullptr);

    void initiateSendFile(const QString &absoluteFilePath);
    void saveReceivedFile(const QString &originalFileName, const std::vector<char> &fileData);

    QString getOutputDirectory() const { return outputDir_; }

signals:
    void sendFileInitiationCompleted(QString filePath, bool success, QString errorReason);
    void receivedFileSaveCompleted(QString originalFileName, QString savedFilePath, bool success, QString errorReason);

private:
    friend class MainWindow;

    static std::tuple<QString, bool, QString> workerSendFileInitiation(ClientSocket *socket, QString filePath);
    static std::tuple<QString, QString, bool, QString> workerSaveReceivedFile(
        QString originalFileName, const std::vector<char> &fileDataVec, QString outputDir);

    ClientSocket &socket_;
    QString outputDir_;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ClientSocket &sock, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isAFileSelected() const;
    QString getSelectedFilePath() const;
    TaskManager &getTaskManager() { return taskManager_; }
    QListWidget *getTaskListWidget() { return taskListWidget_; }
    map<QString, QListWidgetItem *> &getActiveSendTaskItems() { return activeSendTaskItems_; }
    QMutex &getActiveSendTaskItemsMutex() { return activeSendTasksMutex_; }

    void updateMainWindowUITaskItem(QListWidgetItem *item, UITaskStatus status, const QString &displayText, const QString &toolTipText = QString(), const QString &errorMsgForRole = QString());

private slots:
    void onTreeViewClicked(const QModelIndex &index);
    void onListViewDoubleClicked(const QModelIndex &index);
    void onPathLineEditReturnPressed();
    void onGoUpActionTriggered();
    void onListViewSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

    void onTaskListItemDoubleClicked(QListWidgetItem *item);
    void onClearTasksButtonClicked();

    void onSendFileInitiationCompleted(QString filePath, bool success, QString errorReason);
    void onReceivedFileSaveCompleted(QString originalFileName, QString savedFilePath, bool success, QString errorReason);

    void handleNavigationFinished();
    void handleOpenFileFinished();

    void executeActionTask();

private:
    void setupUi();
    void connectSignalsAndSlots();
    void createToolbarActions();

    void handleServerFileResponse(const string &originalFileNameFromServer_std, const vector<char> &fileData, bool serverProcessingSuccess, const string &serverMessage_std);

    void startNavigateToPath(const QString &path);
    void startOpenFile(const QString &filePath);

    void updateUITaskItem(QListWidgetItem *item, UITaskStatus status, const QString &displayText, const QString &toolTipText = QString(), const QString &errorMsgForRole = QString());
    QListWidgetItem *findActiveSendingTaskByOriginalName(const QString &originalFileName);

    static pair<bool, QString> performNavigationTask(QString path);

    QFileSystemModel *fsTreeModel_ = nullptr;
    QFileSystemModel *fsListModel_ = nullptr;
    QTreeView *fsTree_ = nullptr;
    QListView *fsList_ = nullptr;
    QLineEdit *pathEdit_ = nullptr;
    QListWidget *taskListWidget_ = nullptr;

    QAction *upAction_ = nullptr;

    QAction *sendAction_ = nullptr;

    ClientSocket &clientSocketInstance_;
    QString outputDirectory_;
    TaskManager taskManager_;

    map<QString, QListWidgetItem *> activeSendTaskItems_;
    QMutex activeSendTasksMutex_;

    QFutureWatcher<pair<bool, QString>> navigationWatcher_;
    QFutureWatcher<bool> openFileWatcher_;

    map<QAction *, unique_ptr<ActionTask>> toolbarActionTasks_;
};

class SendFileTask : public ActionTask
{
    Q_OBJECT

public:
    explicit SendFileTask(QObject *parent = nullptr);

    void execute(MainWindow *mainWindowContext) override;

    bool canExecute(MainWindow *mainWindowContext) const override;

    QString actionText() const override;

    QIcon actionIcon() const override;
};

int runMainWindow(ClientSocket &client, const vector<string> &args);

#endif
