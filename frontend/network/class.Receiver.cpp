#define _CLASS_RECEIVER_CPP
#include "network.hpp"

void ClientSocket::Receiver::recv_loop()
{
    log_write_regular_information("Receive thread started.");
    int current_sockfd = owner_.sockfd_.load(memory_order_relaxed);
    if (current_sockfd == -1)
    {
        log_write_error_information("Receive loop cannot start: Socket is not valid (-1).");
        return;
    }

    struct pollfd pfd;
    pfd.fd = current_sockfd;
    pfd.events = POLLIN | POLLPRI;
    const int poll_timeout_ms = 200;

    while (!owner_.stop_requested_.load(memory_order_relaxed))
    {
        // Klee酱注意: 每次循环重新获取 sockfd，以防重连时 fd 变化
        // Klee-chan Note: Re-fetch sockfd each loop in case fd changes on reconnect
        current_sockfd = owner_.sockfd_.load(memory_order_relaxed);
        if (current_sockfd == -1)
        {
            log_write_warning_information("Receive loop stopping: Socket became invalid (-1).");
            break; // Socket 无效，退出循环 Socket invalid, exit loop
        }
        pfd.fd = current_sockfd; // 更新 poll 的 fd Update poll's fd
        pfd.revents = 0;

        int poll_ret = ::poll(&pfd, 1, poll_timeout_ms);

        if (owner_.stop_requested_.load(memory_order_relaxed))
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
            continue; // Timeout
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            string err_msg = "Socket error or hangup event";
            int socket_error = 0;
            socklen_t len = sizeof(socket_error);
            if (getsockopt(current_sockfd, SOL_SOCKET, SO_ERROR, &socket_error, &len) == 0 && socket_error != 0)
            {
                err_msg = "Socket error: " + errno_to_string(socket_error);
            }
            else if (pfd.revents & POLLNVAL)
            {
                err_msg = "Socket invalid (POLLNVAL)";
            }
            log_write_error_information(err_msg);
            owner_.trigger_error_callback_internal(err_msg);
            owner_.request_disconnect_async_internal("Socket error event");
            break;
        }

        if (pfd.revents & (POLLIN | POLLPRI))
        {
            int saved_errno = 0;
            // Klee酱注意: read_fd 现在是 Buffer 的方法
            // Klee-chan Note: read_fd is now a method of Buffer
            ssize_t n = recv_buffer_.read_fd(current_sockfd, &saved_errno);

            if (n > 0)
            {
                // 将处理任务交给 MessageHandler
                // Delegate processing task to MessageHandler
                owner_.message_handler_->process_received_data(recv_buffer_);
            }
            else if (n == 0)
            {
                log_write_regular_information("Connection closed by peer (EOF).");
                owner_.request_disconnect_async_internal("Peer closed connection");
                break;
            }
            else
            { // n < 0
                if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
                    continue;
                if (saved_errno == EINTR)
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
