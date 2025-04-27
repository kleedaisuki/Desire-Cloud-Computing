#ifndef _CLOUD_COMPILE_BACKEND_HPP
#define _CLOUD_COMPILE_BACKEND_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <chrono>
#include <iomanip>

#include <string>
#include <string.h>
#include <list>
#include <queue>

#include <type_traits>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <future>

#include "frontend-defs.hpp"
using namespace std;

void log_write_error_information(const string &err);
void log_write_error_information(string &&err);
void log_write_regular_information(const string &info);
void log_write_regular_information(string &&info);

class Socket
{
private:
    int sockfd_;

    void initialize_socket_info()
    {
        sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        if (getpeername(sockfd_, (struct sockaddr *)&peer_addr, &peer_len) == 0)
        {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer_addr.sin_addr, ip_str, sizeof(ip_str));

            stringstream buffer;
            buffer << "Socket: Connection established with " << ip_str << ":" << ntohs(peer_addr.sin_port) << " on fd " << sockfd_ << endl;
            log_write_regular_information(buffer.str());
        }
        else
        {
            stringstream buffer;
            buffer << "Socket: Connection established on fd " << sockfd_ << endl;
            log_write_regular_information(buffer.str());
        }
    }

public:
    Socket(int sockfd) : sockfd_(sockfd) { initialize_socket_info(); }

    Socket(const char *ip, int port) : sockfd_(-1)
    {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0)
        {
            string error_info = "Socket Client Error: Failed to create socket - " + string(strerror(errno));
            log_write_error_information(error_info);
            throw runtime_error(error_info);
        }

        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
        {
            close(sockfd_);
            sockfd_ = -1;
            string error_info = "Socket Client Error: Invalid server address format";
            log_write_error_information(error_info);
            throw runtime_error(error_info);
        }

        if (connect(sockfd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            int connect_errno = errno;
            close(sockfd_);
            sockfd_ = -1;
            string error_info = "Socket Client Error: Failed to connect to server - " + string(strerror(connect_errno));
            log_write_error_information(error_info);
            throw runtime_error(error_info);
        }

        stringstream buffer;
        buffer << "Socket Client: Successfully connected to " << ip << ":" << port << endl;
        log_write_regular_information(buffer.str());
        initialize_socket_info();
    }

    ~Socket()
    {
        if (sockfd_ >= 0)
        {
            close(sockfd_);
            log_write_regular_information("Socket: Closed connection");
            sockfd_ = -1;
        }
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    Socket(Socket &&other) noexcept : sockfd_(other.sockfd_) { other.sockfd_ = -1; }

    Socket &operator=(Socket &&other) noexcept
    {
        if (this != &other)
        {
            if (sockfd_ >= 0)
                close(sockfd_);
            sockfd_ = other.sockfd_;
            other.sockfd_ = -1;
        }
        return *this;
    }

    void send(const string &message)
    {
        ssize_t total_bytes_sent = 0;
        size_t message_len = message.length();
        const char *buffer = message.c_str();

        while (total_bytes_sent < static_cast<ssize_t>(message_len))
        {
            ssize_t bytes_sent = ::send(sockfd_, buffer + total_bytes_sent, message_len - total_bytes_sent, MSG_NOSIGNAL);

            if (bytes_sent < 0)
            {
                if (errno == EINTR)
                    continue;
                string error_info = "Socket Error: Failed to send data - " + string(strerror(errno));
                log_write_error_information(error_info);
                throw runtime_error(error_info);
            }
            else if (bytes_sent == 0)
            {
                string error_info = "Socket Error: Send returned 0, connection might be closed.";
                log_write_error_information(error_info);
                throw runtime_error(error_info);
            }
            total_bytes_sent += bytes_sent;
        }
    }

    stringstream receive(void)
    {
        stringstream ss;
        char buffer[REGULAR_BUFFER_SIZE];
        ssize_t bytes_received;

        while (true)
        {
            bytes_received = ::recv(sockfd_, buffer, REGULAR_BUFFER_SIZE, 0);

            if (bytes_received < 0)
            {
                if (errno == EINTR)
                    continue;
                string err = "Socket Error: Failed to receive data - " + string(strerror(errno));
                log_write_error_information(err);
                throw runtime_error(err);
            }
            else if (bytes_received == 0)
            {
                log_write_regular_information("Socket: Connection closed by peer (recv returned 0).\n");
                break;
            }
            else
                ss.write(buffer, bytes_received);
        } 

        return ss;
    }
};

#ifndef _GLOBAL_CONSTANT_CPP
extern mutex log_mutex;
extern fstream log_file;
extern bool main_thread_stop_flag;
#endif

void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

void initialize(void) throws(runtime_error);
void finalize(void) throws(runtime_error);

#endif
