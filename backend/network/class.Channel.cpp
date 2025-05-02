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
