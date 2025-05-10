// Copyright (C) [2025] [@kleedaisuki] <kleedaisuki@outlook.com>
// This file is part of Simple-K Cloud Executor.
//
// Simple-K Cloud Executor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Simple-K Cloud Executor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Simple-K Cloud Executor.  If not, see <https://www.gnu.org/licenses/>.

#include "cloud-compile-frontend.hpp"

int main(int argc, char *argv[])
{
    ClientSocket client(SERVER_IP, DEFAULT_PORT);
    client.register_default_handler([](const string &payload)
                                    { log_write_warning_information("(client default handler) message received but no tag met: " + payload); });

    client.register_handler("Hello", [](const string &payload)
                            { log_write_regular_information("Client received Hello from server: " + payload); });
    client.register_handler("error-information", [](const string &payload)
                            { log_write_error_information("Client received error-information from server: " + payload); });

    vector<string> args;
    return runMainWindow(client, args);
}

struct global
{
    global(void)
    {
        using namespace filesystem;

        if (!exists("cpl-log"))
            create_directory("cpl-log");
        if (!exists("bin"))
            create_directory("bin");
        if (!exists("src"))
            create_directory("src");
        if (!exists("out"))
            create_directory("out");

        make_sure_log_file();
        log_write_regular_information("Program Starts");
    }

    ~global(void)
    {
        close_log_file();
    }
} automatic;
