#define _CLASS_RECEIVER_CPP
#include "network.hpp"

void ClientSocket::Receiver::recv_loop()
{
    log_write_regular_information("Receive thread started.");
    int current_sockfd = owner_.sockfd_.load(std::memory_order_relaxed);
    if (current_sockfd == -1)
    {
        log_write_error_information("Receive loop cannot start: Socket is not valid (-1).");
        return;
    }
    struct pollfd pfd{current_sockfd, POLLIN | POLLPRI, 0};
    const int poll_timeout_ms = 200;
    while (!owner_.stop_requested_.load(std::memory_order_relaxed))
    {
        current_sockfd = owner_.sockfd_.load(std::memory_order_relaxed);
        if (current_sockfd == -1)
        {
            log_write_warning_information("Receive loop stopping: Socket became invalid (-1).");
            break;
        }
        pfd.fd = current_sockfd;
        pfd.revents = 0;
        int poll_ret = poll(&pfd, 1, poll_timeout_ms);
        if (owner_.stop_requested_.load(std::memory_order_relaxed))
            break;
        if (poll_ret < 0)
        {
            if (errno == EINTR)
                continue;
            log_write_error_information("Poll failed: " + errno_to_string(errno));
            owner_.trigger_error_callback_internal("Poll operation failed: " + errno_to_string(errno));
            owner_.request_disconnect_async_internal("Poll failure");
            break;
        }
        else if (poll_ret == 0)
        {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            std::string err_msg = "Socket error or hangup event";
            int socket_error = 0;
            socklen_t len = sizeof(socket_error);
            if (getsockopt(current_sockfd, SOL_SOCKET, SO_ERROR, &socket_error, &len) == 0 && socket_error != 0)
                err_msg = "Socket error: " + errno_to_string(socket_error);
            else if (pfd.revents & POLLNVAL)
                err_msg = "Socket invalid (POLLNVAL)";
            log_write_error_information(err_msg);
            owner_.trigger_error_callback_internal(err_msg);
            owner_.request_disconnect_async_internal("Socket error event");
            break;
        }
        if (pfd.revents & (POLLIN | POLLPRI))
        {
            int saved_errno = 0;
            ssize_t n = recv_buffer_.read_fd(current_sockfd, &saved_errno);
            if (n > 0)
            {
                if (owner_.message_handler_)
                    owner_.message_handler_->process_received_data(recv_buffer_);
                else      
                    log_write_error_information("MessageHandler is null, cannot process received data."); /* 可能需要断开? */
            }
            else if (n == 0)
            {
                log_write_regular_information("Connection closed by peer (EOF).");
                owner_.request_disconnect_async_internal("Peer closed connection");
                break;
            }
            else
            {
                if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK || saved_errno == EINTR)
                    continue;
                log_write_error_information("Recv failed: " + errno_to_string(saved_errno));
                owner_.trigger_error_callback_internal("Receive operation failed: " + errno_to_string(saved_errno));
                owner_.request_disconnect_async_internal("Receive failure");
                break;
            }
        }
    }
    log_write_regular_information("Receive thread finished.");
}
