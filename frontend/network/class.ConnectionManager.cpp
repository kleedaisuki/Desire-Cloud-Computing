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

#define _CLASS_CONNECTIONMANAGER_CPP
#include "network.hpp"

int ClientSocket::ConnectionManager::try_connect(void)
{
    int temp_sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (temp_sockfd < 0)
    {
        log_write_error_information("Failed to create socket: " + errno_to_string(errno));
        return -1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(owner_.server_port_);
    if (inet_pton(AF_INET, owner_.server_ip_.c_str(), &server_addr.sin_addr) <= 0)
    {
        log_write_error_information("Invalid server IP address format (inet_pton failed).");
        close(temp_sockfd);
        return -1;
    }

    if (::connect(temp_sockfd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    {
        log_write_error_information("Failed to connect to server: " + errno_to_string(errno));
        close(temp_sockfd);
        return -1;
    }

    int flags = fcntl(temp_sockfd, F_GETFL, 0);
    if (flags != -1)
        fcntl(temp_sockfd, F_SETFL, flags | O_NONBLOCK);
    else
    {
        log_write_warning_information("Failed to get socket flags (F_GETFL): " + errno_to_string(errno));
        if (fcntl(temp_sockfd, F_SETFL, O_NONBLOCK) == -1)
            log_write_warning_information("Failed to set socket non-blocking (F_SETFL): " + errno_to_string(errno));
    }
    return temp_sockfd;
}
