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

#define _CLASS_CHANNEL_CPP
#include "network.hpp"
using namespace net;

void Channel::handle_event()
{
    shared_ptr<void> guard;
    if (tied_)
    {
        guard = tie_.lock();
        if (!guard)
        {
            log_write_warning_information("Channel::handle_event() - Tied object expired, fd = " + to_string(fd_));
            return;
        }
    }

    log_write_regular_information("handle_event revents=" + to_string(revents_) + " for fd=" + to_string(fd_));

    if (revents_ & (EPOLLERR | EPOLLHUP))
    {
        if (!(revents_ & EPOLLIN))
            log_write_warning_information("Channel::handle_event() EPOLLHUP without EPOLLIN fd = " + to_string(fd_));

        if (error_cb_)
            error_cb_();
    }

    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
        if (read_cb_)
            read_cb_();

    if (revents_ & EPOLLOUT)
        if (write_cb_)
            write_cb_();
}
