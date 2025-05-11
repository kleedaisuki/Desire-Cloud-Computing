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

#define _CLASS_SENDFILETASK_CPP
#include "graphic-interface.hpp"

SendFileTask::SendFileTask(QObject *parent)
    : ActionTask(parent)
{
    log_write_regular_information("SendFileTask instance created.");
}

QString SendFileTask::actionText() const
{
    return "发送文件";
}

QIcon SendFileTask::actionIcon() const
{
    return QIcon::fromTheme("document-send", QIcon(":/qt-project.org/styles/commonstyle/images/network-transmit-16.png"));
}

bool SendFileTask::canExecute(MainWindow *mainWindowContext) const
{
    if (!mainWindowContext)
    {
        log_write_error_information("[SendFileTask] cannot check canExecute: mainWindowContext is null.");
        return false;
    }
    return mainWindowContext->isAFileSelected();
}

void SendFileTask::execute(MainWindow *mainWindowContext)
{
    if (!mainWindowContext)
    {
        log_write_error_information("[SendFileTask] cannot execute: mainWindowContext is null.");
        return;
    }

    if (!canExecute(mainWindowContext))
    {
        log_write_warning_information("[SendFileTask] execute called but canExecute is false. Aborting.");
        QMessageBox::information(mainWindowContext, "提示", "请先选择一个文件才能发送哦！");
        return;
    }

    QString filePath = mainWindowContext->getSelectedFilePath();
    if (filePath.isEmpty())
    {
        log_write_error_information("[SendFileTask] execute: Selected file path is empty, though canExecute was true. This is unexpected.");
        QMessageBox::warning(mainWindowContext, "错误", "未能获取选中的文件路径。");
        return;
    }

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    log_write_regular_information("[SendFileTask] Initiating send for: " + filePath.toStdString());

    {
        QMutexLocker locker(&mainWindowContext->getActiveSendTaskItemsMutex());
        if (mainWindowContext->getActiveSendTaskItems().count(fileName))
        {
            QMessageBox::warning(mainWindowContext, "任务已存在",
                                 QString("文件 '%1' 的发送任务已经在进行中或等待服务器响应。").arg(fileName));
            log_write_warning_information("[SendFileTask] Send request for " + fileName.toStdString() + " aborted, task already exists.");
            return;
        }
    }

    QListWidgetItem *newItem = new QListWidgetItem();
    newItem->setData(OriginalFileNameRole, fileName);
    newItem->setData(FilePathRole, filePath);

    QListWidget *taskListWidget_ptr = mainWindowContext->getTaskListWidget();
    if (taskListWidget_ptr)
    {
        taskListWidget_ptr->addItem(newItem);
    }
    else
    {
        log_write_error_information("[SendFileTask] TaskListWidget is null in MainWindow context.");
        delete newItem;
        return;
    }

    mainWindowContext->updateMainWindowUITaskItem(newItem, UITaskStatus::Preparing, QString("准备发送 (Preparing to send): %1").arg(fileName));

    {
        QMutexLocker locker(&mainWindowContext->getActiveSendTaskItemsMutex());
        mainWindowContext->getActiveSendTaskItems()[fileName] = newItem;
    }

    mainWindowContext->getTaskManager().initiateSendFile(filePath);
    log_write_regular_information("[SendFileTask] File send initiated for: " + fileName.toStdString());
}
