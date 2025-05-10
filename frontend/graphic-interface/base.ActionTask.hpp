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

#ifndef _ACTION_TASK_BASE_HPP
#define _ACTION_TASK_BASE_HPP

#include <QString>
#include <QIcon>
#include <QObject>

class MainWindow;

/**
 * @brief Abstract base class for action tasks.
 *
 * Represents an action that can be triggered by the UI, e.g., a toolbar button.
 */
class ActionTask : public QObject 
{
    Q_OBJECT

public:
    /**
     * @brief Constructor.
     * @param parent Parent QObject for memory management.
     */
    explicit ActionTask(QObject* parent = nullptr) : QObject(parent) {}

    /**
     * @brief Virtual destructor.
     */
    virtual ~ActionTask() = default;

    /**
     * @brief Core logic for executing the task.)
     *
     * Subclasses must implement this method to define specific task behavior.
     * @param mainWindowContext Pointer to the MainWindow instance, allowing the task to access main window resources and state.
     */
    virtual void execute(MainWindow* mainWindowContext) = 0;

    /**
     * @brief Checks if the task can be executed in the current state. 
     * 
     * For example, a send file task might only be executable if a file is selected.
     * @param mainWindowContext Pointer to the MainWindow instance.
     * @return Returns true if the task can be executed, false otherwise.
     */
    virtual bool canExecute(MainWindow* mainWindowContext) const
    {
        Q_UNUSED(mainWindowContext); // Avoid unused parameter warning
        return true; // Default is always executable
    }

    /**
     * @brief Gets the text to be displayed on the toolbar button for this task.
     * @return QString for the button text.
     */
    virtual QString actionText() const = 0;

    /**
     * @brief Gets the icon to be displayed on the toolbar button for this task.
     * @return QIcon for the button icon.
     */
    virtual QIcon actionIcon() const = 0;
};

#endif