#define _CLASS_SENDER_CPP
#include "network.hpp"

void ClientSocket::Sender::send_loop()
{
    log_write_regular_information("Send thread started.");
    while (!owner_.stop_requested_.load(memory_order_relaxed))
    {
        vector<char> message_to_send;
        {
            unique_lock<mutex> lock(send_mutex_);
            send_cv_.wait(lock, [this]
                          { return owner_.stop_requested_.load(memory_order_relaxed) || !send_queue_.empty(); });

            if (owner_.stop_requested_.load(memory_order_relaxed))
                break;

            message_to_send = move(send_queue_.front());
            send_queue_.pop();
        }

        if (!send_all_internal(message_to_send.data(), message_to_send.size()))
        {
            log_write_error_information("Send failed, likely disconnected. Stopping send loop.");
            owner_.trigger_error_callback_internal("Send operation failed.");
            owner_.request_disconnect_async_internal("Send failure");
            break;
        }
    }
    log_write_regular_information("Send thread finished.");
}

bool ClientSocket::Sender::send_all_internal(const char *data, size_t len)
{
    size_t total_sent = 0;
    int current_sockfd = owner_.sockfd_.load(memory_order_relaxed);

    while (total_sent < len and !owner_.stop_requested_.load(memory_order_relaxed))
    {
        if (current_sockfd == -1)
        {
            log_write_error_information("Send failed: Socket is not valid (-1).");
            return false;
        }

        ssize_t sent = ::send(current_sockfd, data + total_sent, len - total_sent, MSG_NOSIGNAL);

        if (sent > 0)
        {
            total_sent += static_cast<size_t>(sent);
            continue;
        }
        if (sent == 0)
        {
            log_write_warning_information("Send returned 0 unexpectedly.");
            return false;
        }

        int current_errno = errno;
        if (current_errno == EAGAIN || current_errno == EWOULDBLOCK)
        {
            struct pollfd pfd;
            pfd.fd = current_sockfd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int poll_ret = ::poll(&pfd, 1, 100);

            if (poll_ret > 0 and (pfd.revents & POLLOUT))
                continue;
            else if (poll_ret == 0)
                continue;
            else
            {
                if (errno == EINTR and !owner_.stop_requested_.load(memory_order_relaxed))
                    continue;
                log_write_error_information("Poll failed while waiting to send or socket error: " + errno_to_string(errno));
                return false;
            }
        }
        else if (current_errno == EINTR and !owner_.stop_requested_.load(memory_order_relaxed))
            continue;
        else
        {
            log_write_error_information("Send failed: " + errno_to_string(current_errno));
            return false;
        }
    }
    return total_sent == len;
}
