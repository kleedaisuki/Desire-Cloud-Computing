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

#define __CLASS_TASKMANAGER_CPP
#include "graphic-interface.hpp"

TaskManager::TaskManager(ClientSocket &sock, const QString &outDir, QObject *parent)
    : QObject(parent), socket_(sock), outputDir_(outDir)
{
    if (!QDir(outputDir_).exists() && !QDir().mkpath(outputDir_))
    {
        log_write_error_information("TaskManager: Failed to create output directory: " + outputDir_.toStdString());
    }
}

void TaskManager::initiateSendFile(const QString &absoluteFilePath)
{
    log_write_regular_information("TaskManager: Queuing send initiation for: " + absoluteFilePath.toStdString());
    auto *watcher = new QFutureWatcher<tuple<QString, bool, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]()
            {
        auto result = watcher->result();
        emit sendFileInitiationCompleted(get<0>(result), get<1>(result), get<2>(result));
        watcher->deleteLater(); });
    watcher->setFuture(QtConcurrent::run(
        &TaskManager::workerSendFileInitiation, &socket_, absoluteFilePath));
}

void TaskManager::saveReceivedFile(const QString &originalFileName, const vector<char> &fileData)
{
    log_write_regular_information("TaskManager: Queuing save operation for received file (original name): " + originalFileName.toStdString());
    auto *watcher = new QFutureWatcher<tuple<QString, QString, bool, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]()
            {
        auto result = watcher->result();
        emit receivedFileSaveCompleted(get<0>(result), get<1>(result), get<2>(result), get<3>(result));
        watcher->deleteLater(); });
    watcher->setFuture(QtConcurrent::run(
        &TaskManager::workerSaveReceivedFile, originalFileName, fileData, outputDir_));
}

tuple<QString, bool, QString> TaskManager::workerSendFileInitiation(ClientSocket *socket, QString filePath)
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
        catch (const exception &e)
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

tuple<QString, QString, bool, QString> TaskManager::workerSaveReceivedFile(
    QString originalFileName, const vector<char> &fileDataVec, QString outputDir)
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

        file.flush();
        file.close();

        if (bytesWritten != static_cast<qint64>(fileDataVec.size()))
        {
            errorMsg = QString("Failed to write all data to file: %1. Written: %2, Expected: %3. File error: %4")
                           .arg(savePath)
                           .arg(bytesWritten)
                           .arg(fileDataVec.size())
                           .arg(file.errorString());
        }
        else if (file.error() != QFileDevice::NoError)
        {
            errorMsg = QString("File operation reported error after write/close: %1. Error: %2")
                           .arg(savePath, file.errorString());
        }
        else
        {
            success = true;
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
