#define _CLASS_TCPCONNECTION_CPP
#include "network.hpp"
using namespace net;

TcpConnection::TcpConnection(EventLoop *loop,
                             string name,
                             int sockfd,
                             const sockaddr_in &local_addr,
                             const sockaddr_in &peer_addr)
    : loop_{loop},
      name_{move(name)},
      state_{State::kConnecting},
      reading_{true}, // Default to reading
      socket_{sockfd},
      channel_{make_unique<Channel>(loop, sockfd)},
      local_addr_{local_addr},
      peer_addr_{peer_addr},
      high_water_mark_{64 * 1024 * 1024}
{
    channel_->on_read([this]
                      { handle_read(); });
    channel_->on_write([this]
                       { handle_write(); });
    channel_->on_error([this]
                       { handle_error(); });

    log_write_regular_information("TcpConnection::ctor[" + name_ + "] at " +
                                  to_string(reinterpret_cast<uintptr_t>(this)) +
                                  " fd=" + to_string(sockfd));
    if (util::set_non_blocking(sockfd) == -1)
        log_write_error_information("Failed to set non-blocking for fd " + to_string(sockfd) + " in TcpConnection ctor.");
}

TcpConnection::~TcpConnection()
{
    log_write_regular_information("TcpConnection::dtor[" + name_ + "] at " +
                                  to_string(reinterpret_cast<uintptr_t>(this)) +
                                  " fd=" + (channel_ ? to_string(channel_->fd()) : "n/a") +
                                  " state=" + to_string(static_cast<int>(state_)));
    assert(state_ == State::kDisconnected);
}

void TcpConnection::connect_established()
{
    loop_->assert_in_loop_thread();
    assert(state_ == State::kConnecting);
    set_state(State::kConnected);
    channel_->tie(shared_from_this());
    channel_->enable_reading();
    if (connection_cb_)
        connection_cb_(shared_from_this());
}

void TcpConnection::connect_destroyed()
{
    loop_->assert_in_loop_thread();
    bool was_connected = (state_ == State::kConnected);
    set_state(State::kDisconnected);

    if (channel_)
    {
        if (!channel_->is_none_event())
            channel_->disable_all();
        channel_->remove();
    }

    if (was_connected and connection_cb_)
        connection_cb_(shared_from_this());
    log_write_regular_information("TcpConnection::connect_destroyed [" + name_ + "] fd=" + (channel_ ? to_string(channel_->fd()) : "n/a"));
}

void TcpConnection::send(string_view message)
{
    if (state_ == State::kConnected)
    {
        if (loop_->is_in_loop_thread())
            send_in_loop(message.data(), message.size());
        else
        {
            string msg_copy = string(message); // Must copy for cross-thread
            loop_->run_in_loop([ptr = shared_from_this(), msg = move(msg_copy)]()
                               { ptr->send_in_loop(msg.data(), msg.size()); });
        }
    }
    else
        log_write_warning_information("TcpConnection::send [" + name_ + "] - Connection disconnected, cannot send.");
}

void TcpConnection::send(Buffer *buf)
{
    if (state_ == State::kConnected)
    {
        if (loop_->is_in_loop_thread())
        {
            send_in_loop(buf->peek(), buf->readable_bytes());
            buf->retrieve_all();
        }
        else
        {
            string msg_copy = buf->retrieve_all_as_string(); // Copy buffer content
            loop_->run_in_loop([ptr = shared_from_this(), msg = move(msg_copy)]()
                               { ptr->send_in_loop(msg.data(), msg.size()); });
        }
    }
    else
        log_write_warning_information("TcpConnection::send(Buffer*) [" + name_ + "] - Connection disconnected, cannot send.");
}

void TcpConnection::send_in_loop(const void *data, size_t len)
{
    loop_->assert_in_loop_thread();
    if (state_ == State::kDisconnected or state_ == State::kDisconnecting)
    {
        log_write_warning_information("TcpConnection::send_in_loop [" + name_ + "] - disconnected or disconnecting, give up writing.");
        return;
    }

    ssize_t nwrote = 0;
    size_t remaining = len;
    bool fault_error = false;

    const char *char_data = static_cast<const char *>(data);

    if (!channel_->is_writing() and output_buffer_.readable_bytes() == 0)
    {
        nwrote = ::write(channel_->fd(), char_data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 and write_complete_cb_)
            {
                loop_->queue_in_loop([ptr = shared_from_this()]()
                                     {
                                        if(ptr->write_complete_cb_) 
                                        ptr->write_complete_cb_(ptr); });
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK and errno != EAGAIN)
            {
                log_write_error_information("TcpConnection::send_in_loop [" + name_ + "] write error: " + errno_to_string(errno));
                if (errno == EPIPE or errno == ECONNRESET)
                    fault_error = true;
            }
        }
    }

    assert(remaining <= len);

    if (!fault_error and remaining > 0)
    {
        size_t old_len = output_buffer_.readable_bytes();
        if (old_len + remaining >= high_water_mark_ and
            old_len < high_water_mark_ and
            high_water_mark_cb_)
        {
            loop_->queue_in_loop([ptr = shared_from_this(), current_len = old_len + remaining]()
                                 {
if(ptr->high_water_mark_cb_) ptr->high_water_mark_cb_(ptr, current_len); });
        }
        output_buffer_.append(char_data + nwrote, remaining);
        if (!channel_->is_writing())
        {
            channel_->enable_writing();
        }
    }
    else if (fault_error)
        handle_error();
}

void TcpConnection::send_in_loop(string_view message)
{
    send_in_loop(message.data(), message.size());
}

void TcpConnection::shutdown()
{
    if (state_ == State::kConnected)
    {
        set_state(State::kDisconnecting);
        loop_->run_in_loop([ptr = shared_from_this()]
                           { ptr->shutdown_in_loop(); });
    }
}

void TcpConnection::shutdown_in_loop()
{
    loop_->assert_in_loop_thread();
    if (!channel_->is_writing())
    { // Only shutdown if not waiting for writes
        if (::shutdown(socket_.fd(), SHUT_WR) < 0)
            log_write_error_information("TcpConnection::shutdown_in_loop [" + name_ + "] SHUT_WR error: " + errno_to_string(errno));
        else
            log_write_regular_information("TcpConnection::shutdown_in_loop [" + name_ + "] - SHUT_WR successful.");
    }
    else
        log_write_regular_information("TcpConnection::shutdown_in_loop [" + name_ + "] - Waiting for writes to complete before shutdown.");
}

void TcpConnection::force_close()
{
    if (state_ == State::kConnected or state_ == State::kDisconnecting)
    {
        set_state(State::kDisconnecting);
        loop_->queue_in_loop([ptr = shared_from_this()]()
                             { ptr->force_close_in_loop(); });
    }
}

void TcpConnection::force_close_in_loop()
{
    loop_->assert_in_loop_thread();
    log_write_regular_information("TcpConnection::force_close_in_loop [" + name_ + "] fd=" + to_string(channel_->fd()));
    handle_close();
}

void TcpConnection::handle_read()
{
    loop_->assert_in_loop_thread();
    int saved_errno = 0;
    ssize_t n = input_buffer_.read_fd(channel_->fd(), &saved_errno);

    if (n > 0)
    {
        if (message_cb_)
        {
            auto self = shared_from_this();
            auto fut = ThreadPool::instance().enqueue(
                [this, self, buf_ptr = &input_buffer_]() -> string
                {
                    return message_cb_(self, buf_ptr);
                });

            try
            {
                string response = fut.get();
                if (!response.empty())
                    send(response);
            }
            catch (const future_error &e)
            {
                log_write_error_information("Future error getting result for connection [" + name_ + "]: " + string(e.what()) + " code: " + to_string(e.code().value()));
                send("Error processing request (future).\r\n");
            }
            catch (const exception &e)
            {
                log_write_error_information("MessageCallback exception for connection [" + name_ + "]: " + string(e.what()));
                send("Error processing request.\r\n");
            }
            catch (...)
            {
                log_write_error_information("Unknown exception during MessageCallback for connection [" + name_ + "]");
                send("Unknown error processing request.\r\n");
            }
        }
        else
        {
            log_write_warning_information("No message callback set for connection [" + name_ + "], discarding " + to_string(n) + " bytes.");
            input_buffer_.retrieve_all();
        }
    }
    else if (n == 0)
        handle_close();
    else
    {
        errno = saved_errno;
        log_write_error_information("TcpConnection::handle_read [" + name_ + "] read error: " + errno_to_string(errno));
        handle_error();
    }
}

void TcpConnection::handle_write()
{
    loop_->assert_in_loop_thread();
    if (channel_->is_writing())
    {
        ssize_t n = ::write(channel_->fd(),
                            output_buffer_.peek(),
                            output_buffer_.readable_bytes());
        if (n > 0)
        {
            output_buffer_.retrieve(n);
            if (output_buffer_.readable_bytes() == 0)
            {
                channel_->disable_writing();
                if (write_complete_cb_)
                {
                    loop_->queue_in_loop([ptr = shared_from_this()]()
                                         {
                                            if(ptr->write_complete_cb_) 
                                                ptr->write_complete_cb_(ptr); });
                }
                if (state_ == State::kDisconnecting)
                    shutdown_in_loop();
            }
            else
                log_write_regular_information("TcpConnection::handle_write [" + name_ + "] - more data to write: " + to_string(output_buffer_.readable_bytes()));
        }
        else
        {
            log_write_error_information("TcpConnection::handle_write [" + name_ + "] write error: " + errno_to_string(errno));
            if (errno != EWOULDBLOCK and errno != EAGAIN)
                handle_error();
        }
    }
    else
        log_write_warning_information("TcpConnection::handle_write [" + name_ + "] - channel is not writing, fd = " + to_string(channel_->fd()));
}

void TcpConnection::handle_close()
{
    loop_->assert_in_loop_thread();
    log_write_regular_information("TcpConnection::handle_close [" + name_ + "] fd = " + to_string(channel_->fd()) +
                                  " state = " + to_string(static_cast<int>(state_)));
    if (state_ == State::kDisconnected)
        return;
    assert(state_ == State::kConnected or state_ == State::kDisconnecting);

    set_state(State::kDisconnected);
    channel_->disable_all();

    TcpConnectionPtr guard_this(shared_from_this());
    if (connection_cb_)
        connection_cb_(guard_this);
    if (close_cb_)
        close_cb_(guard_this);
}

void TcpConnection::handle_error()
{
    loop_->assert_in_loop_thread();
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
        err = errno;
    else
        err = optval;
    log_write_error_information("TcpConnection::handle_error [" + name_ + "] - SO_ERROR = " + to_string(err) + " (" + errno_to_string(err) + ")");
    handle_close();
}