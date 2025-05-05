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

#define _CLASS_ACCEPTER_CPP
#include "network.hpp"
using namespace net;

#include <iostream>

Acceptor::Acceptor(EventLoop *loop, uint16_t port, bool reuse_port)
    : loop_{loop},
      accept_socket_{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP)},
      accept_channel_{loop, accept_socket_.fd()},
      listening_{false},
      idle_fd_{::open("/dev/null", O_RDONLY | O_CLOEXEC)}
{
    if (accept_socket_.fd() < 0)
        util::fatal_perror("Acceptor::Acceptor socket failed");
    if (idle_fd_ < 0)
        util::fatal_perror("Acceptor::Acceptor open /dev/null failed");

    int optval = 1;
    ::setsockopt(accept_socket_.fd(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (reuse_port)
        ::setsockopt(accept_socket_.fd(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(accept_socket_.fd(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        util::fatal_perror("Acceptor::Acceptor bind failed on port " + to_string(port));

    accept_channel_.on_read([this]
                            { handle_read(); });
    log_write_regular_information("Acceptor created for port " + to_string(port) + ", fd=" + to_string(accept_socket_.fd()));
}

Acceptor::~Acceptor()
{
    log_write_regular_information("Acceptor destroyed with fd=" + to_string(idle_fd_));
    accept_channel_.disable_all();
    accept_channel_.remove();
    ::close(idle_fd_);
}

void Acceptor::listen()
{
    loop_->assert_in_loop_thread();
    listening_ = true;
    if (::listen(accept_socket_.fd(), SOMAXCONN) < 0)
        util::fatal_perror("Acceptor::listen failed");
    accept_channel_.enable_reading();
    log_write_regular_information("Acceptor starts listening on fd " + to_string(accept_socket_.fd()));
}

void Acceptor::handle_read()
{
    loop_->assert_in_loop_thread();
    sockaddr_in peer_addr{};
    socklen_t addrlen = sizeof(peer_addr);

    while (true)
    {
        int connfd = ::accept4(accept_socket_.fd(), reinterpret_cast<sockaddr *>(&peer_addr),
                               &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd >= 0)
        {
            log_write_regular_information("Accepted new connection sockfd=" + to_string(connfd));
            if (new_connection_cb_)
                new_connection_cb_(connfd, peer_addr);
            else
            {
                log_write_warning_information("No NewConnectionCallback set, closing accepted fd " + to_string(connfd));
                ::close(connfd);
            }
        }
        else
        {
            int saved_errno = errno;
            if (saved_errno == EAGAIN or saved_errno == EWOULDBLOCK)
                break;
            else if (saved_errno == EMFILE or saved_errno == ENFILE)
            {
                ::close(idle_fd_);
                idle_fd_ = ::accept(accept_socket_.fd(), nullptr, nullptr);
                ::close(idle_fd_);
                idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                log_write_error_information("Acceptor::handle_read - Reached fd limit (EMFILE/ENFILE), closed one incoming connection.");
                break;
            }
            else if (saved_errno == ECONNABORTED or saved_errno == EINTR or saved_errno == EPROTO)
                log_write_warning_information("Acceptor::handle_read - Ignorable accept error: " + errno_to_string(saved_errno));
            else
            {
                log_write_error_information("FATAL: Acceptor::handle_read accept failed: " + errno_to_string(saved_errno));
                break;
            }
        }
    }
}