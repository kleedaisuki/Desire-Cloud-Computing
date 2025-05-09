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

#define _MAIN_CPP
#include "cloud-compile-backend.hpp"
using namespace net;

int main(int argc, char *argv[])
{
    EventLoop loop;
    TcpServer server(&loop, DEFAULT_PORT, "k-SI");

    server.set_connection_callback([](const TcpConnectionPtr &conn)
                                   {
        if (conn->connected()) 
        {
            char peer_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &conn->peer_address().sin_addr, peer_ip, sizeof(peer_ip));
            uint16_t peer_port = ntohs(conn->peer_address().sin_port);
            log_write_regular_information("Client connected: " + conn->name() + " from " +
                                           string(peer_ip) + ":" + to_string(peer_port));
        } 
        else 
            log_write_regular_information("Client disconnected: " + conn->name()); });

    server.register_protocol_handler("compile-execute", [](const TcpConnectionPtr &conn, const string &tag, string_view payload) -> TcpServer::ProtocolHandlerPair {
        return {"compile-execute", string(payload)};
    });

    server.register_protocol_handler("Hello", [](const TcpConnectionPtr &conn, const string &tag, string_view payload) -> TcpServer::ProtocolHandlerPair
                                     { return {"Hello", "Communication set up!"}; });

    server.start();
    loop.loop();
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
