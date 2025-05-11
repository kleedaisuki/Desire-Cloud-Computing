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

#include "graphic-interface.hpp"

int runMainWindow(ClientSocket &client, const vector<string> &args)
{
    log_write_regular_information("runMainWindow called. Args count: " + to_string(args.size()));
    for (size_t i = 0; i < args.size(); ++i)
    {
        log_write_regular_information("Arg " + to_string(i) + ": " + args[i]);
    }

    vector<char *> argv_vec;
    string appNameStr = "SimpleKFileExplorer";
    argv_vec.push_back(const_cast<char *>(appNameStr.c_str()));

    for (const auto &arg_str : args)
    {
        argv_vec.push_back(const_cast<char *>(arg_str.c_str()));
    }
    int argc_val = static_cast<int>(argv_vec.size());
    char **argv_ptr = argv_vec.data();

    QApplication *app_ptr = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app_ptr)
    {
        app_ptr = new QApplication(argc_val, argv_ptr);
        log_write_regular_information("New QApplication instance created in runMainWindow.");
    }
    else
    {
        log_write_regular_information("Using existing QApplication instance in runMainWindow.");
    }
    QApplication &app = *app_ptr;
    if (app.applicationName().isEmpty())
        app.setApplicationName("Simple-K File Explorer");
    if (app.organizationName().isEmpty())
        app.setOrganizationName("K-Works");

    MainWindow mainWindow(client);
    mainWindow.show();

    log_write_regular_information("MainWindow shown in runMainWindow. Starting Qt event loop...");
    int exitCode = app.exec();
    log_write_regular_information(QString("Qt event loop finished in runMainWindow with exit code: %1.").arg(exitCode).toStdString());
    return exitCode;
}
