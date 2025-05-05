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

#define _CLASS_EVENTLOOP_CPP
#include "network.hpp"
using namespace net;

EventLoop::EventLoop()
    : looping_{false},
      quit_{false},
      event_handling_{false},
      calling_pending_functors_{false},
      thread_id_{this_thread::get_id()},
      epoll_fd_{::epoll_create1(EPOLL_CLOEXEC)},
      wakeup_fd_{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)},
      active_events_(kMaxEvents)
{
    if (epoll_fd_.fd() == -1)
        util::fatal_perror("EventLoop::EventLoop epoll_create1 failed");

    if (wakeup_fd_.fd() == -1)
        util::fatal_perror("EventLoop::EventLoop eventfd failed");
    log_write_regular_information("EventLoop created in thread " + to_string(hash<thread::id>{}(thread_id_)) +
                                  ", epollfd=" + to_string(epoll_fd_.fd()) +
                                  ", wakeupfd=" + to_string(wakeup_fd_.fd()));

    wakeup_channel_ = make_unique<Channel>(this, wakeup_fd_.fd());
    wakeup_channel_->on_read([this]
                             { handle_read(); });
    wakeup_channel_->enable_reading();
}

EventLoop::~EventLoop()
{
    log_write_regular_information("EventLoop destroyed in thread " + to_string(hash<thread::id>{}(this_thread::get_id())));
    assert(!looping_);
}

void EventLoop::loop()
{
    assert(!looping_);
    assert_in_loop_thread();
    looping_ = true;
    quit_ = false;
    log_write_regular_information("EventLoop " + to_string(reinterpret_cast<uintptr_t>(this)) + " start looping in thread " + to_string(hash<thread::id>{}(thread_id_)));

    while (!quit_)
    {
        active_events_.clear();
        int num_events = ::epoll_wait(epoll_fd_.fd(), active_events_.data(), kMaxEvents, -1);
        int saved_errno = errno;

        if (num_events > 0)
        {
            log_write_regular_information(to_string(num_events) + " events happened");
            event_handling_ = true;
            for (int i = 0; i < num_events; ++i)
            {
                Channel *channel = static_cast<Channel *>(active_events_[i].data.ptr);
                channel->set_revents(active_events_[i].events);
                channel->handle_event();
            }
            event_handling_ = false;
        }
        else if (num_events == 0)
            ;
        else
        {
            if (saved_errno != EINTR)
            {
                errno = saved_errno;
                log_write_error_information("EventLoop::loop() epoll_wait() error: " + errno_to_string(errno));
            }
        }
        do_pending_functors();
    }

    log_write_regular_information("EventLoop " + to_string(reinterpret_cast<uintptr_t>(this)) + " stop looping.");
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    if (!is_in_loop_thread())
        wakeup();
}

void EventLoop::run_in_loop(Functor f)
{
    if (is_in_loop_thread())
        f();
    else
        queue_in_loop(move(f));
}

void EventLoop::queue_in_loop(Functor f)
{
    {
        lock_guard lock(mutex_);
        pending_functors_.push_back(move(f));
    }
    if (!is_in_loop_thread() || calling_pending_functors_)
        wakeup();
}

void EventLoop::update_channel(Channel *channel)
{
    assert(channel->owner_loop() == this);
    assert_in_loop_thread();
    const int fd = channel->fd();
    log_write_regular_information("update_channel fd = " + to_string(fd) + " events = " + to_string(channel->events()));

    struct epoll_event ev{};
    ev.events = channel->events();
    ev.data.ptr = channel;

    if (channels_.count(fd))
    {
        if (channel->is_none_event())
        {
            if (::epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_DEL, fd, nullptr) == -1)
                log_write_error_information("epoll_ctl op=DEL fd=" + to_string(fd) + " failed: " + errno_to_string(errno));
        }
        else
        {
            if (::epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_MOD, fd, &ev) == -1)
                log_write_error_information("epoll_ctl op=MOD fd=" + to_string(fd) + " failed: " + errno_to_string(errno));
            log_write_regular_information("MOD fd = " + to_string(fd) + " events = " + to_string(channel->events()));
        }
    }
    else
    {
        channels_[fd] = channel;
        if (::epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            log_write_error_information("epoll_ctl op=ADD fd=" + to_string(fd) + " failed: " + errno_to_string(errno));
            channels_.erase(fd);
        }
        else
            log_write_regular_information("ADD fd = " + to_string(fd) + " events = " + to_string(channel->events()));
    }
}

void EventLoop::remove_channel(Channel *channel)
{
    assert(channel->owner_loop() == this);
    assert_in_loop_thread();
    int fd = channel->fd();
    assert(channels_.count(fd));
    assert(channels_[fd] == channel);
    assert(channel->is_none_event());

    log_write_regular_information("remove_channel fd = " + to_string(fd));
    size_t n = channels_.erase(fd);
    assert(n == 1);

    if (::epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_DEL, fd, nullptr) == -1)
        if (errno != ENOENT)
            log_write_error_information("epoll_ctl op=DEL fd=" + to_string(fd) + " failed during remove_channel: " + errno_to_string(errno));
}

bool EventLoop::has_channel(Channel *channel)
{
    assert(channel->owner_loop() == this);
    assert_in_loop_thread();
    auto it = channels_.find(channel->fd());
    return it != channels_.end() and it->second == channel;
}

void EventLoop::handle_read()
{
    assert_in_loop_thread();
    uint64_t one = 1;
    ssize_t n = ::read(wakeup_fd_.fd(), &one, sizeof one);
    if (n != sizeof one)
        log_write_error_information("EventLoop::handle_read() reads " + to_string(n) + " bytes instead of 8 from wakeup fd " + to_string(wakeup_fd_.fd()));
    log_write_regular_information("EventLoop woken up");
}

void EventLoop::do_pending_functors()
{
    vector<Functor> functors;
    calling_pending_functors_ = true;
    {
        lock_guard lock(mutex_);
        functors.swap(pending_functors_);
    }

    log_write_regular_information("Executing " + to_string(functors.size()) + " pending functors");
    for (const Functor &functor : functors)
        try
        {
            functor();
        }
        catch (const exception &e)
        {
            log_write_error_information("Pending functor exception: " + string(e.what()));
        }
        catch (...)
        {
            log_write_error_information("Pending functor unknown exception.");
        }
    calling_pending_functors_ = false;
}

void EventLoop::abort_not_in_loop_thread()
{
    string error_msg = "EventLoop::abort_not_in_loop_thread - EventLoop " +
                       to_string(reinterpret_cast<uintptr_t>(this)) +
                       " was created in threadId_ = " + to_string(hash<thread::id>{}(thread_id_)) +
                       ", current thread id = " + to_string(hash<thread::id>{}(this_thread::get_id()));
    log_write_error_information("FATAL ERROR: " + error_msg);
    abort();
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(wakeup_fd_.fd(), &one, sizeof one);
    if (n != sizeof(one))
        log_write_error_information("EventLoop::wakeup() writes " + to_string(n) + " bytes instead of 8 to wakeup fd " + to_string(wakeup_fd_.fd()) + ": " + errno_to_string(errno));
    log_write_regular_information("Waking up loop thread");
}
