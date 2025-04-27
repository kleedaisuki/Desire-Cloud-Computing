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

#include "backend-defs.hpp"
using namespace std;

class ThreadPool
{
public:
    enum class priority_level
    {
        NORMAL,
        LOG,
    };

private:
    struct task_with_priority
    {
        function<void()> task;
        priority_level priority;

        task_with_priority(void) : task() { this->priority = static_cast<priority_level>(0); }

        task_with_priority(task_with_priority &&other)
        {
            task = move(other.task);
            priority = other.priority;
        }

        task_with_priority(const task_with_priority &other)
        {
            task = other.task;
            priority = other.priority;
        }

        task_with_priority &operator=(const task_with_priority &other)
        {
            task = other.task;
            priority = other.priority;
            return *this;
        }

        task_with_priority &operator=(task_with_priority &&other)
        {
            task = move(other.task);
            priority = other.priority;
            return *this;
        }

        task_with_priority(function<void()> &&task, priority_level priority)
        {
            this->task = task;
            this->priority = priority;
        }

        bool operator>(const task_with_priority &other) const { return static_cast<int>(priority) > static_cast<int>(other.priority); }
        bool operator<(const task_with_priority &other) const { return static_cast<int>(priority) < static_cast<int>(other.priority); }
        bool operator==(const task_with_priority &other) const { return static_cast<int>(priority) == static_cast<int>(other.priority); }
        bool operator!=(const task_with_priority &other) const { return static_cast<int>(priority) != static_cast<int>(other.priority); }
    };

private:
    vector<thread> threads;
    atomic<size_t> available_threads_count;

    priority_queue<task_with_priority> tasks;
    mutex queue_mutex;

    condition_variable condition;
    bool stop;

    void executer(void)
    {
        task_with_priority task;
        while (true)
        {
            {
                unique_lock<mutex> lock(queue_mutex);
                condition.wait(lock, [this]
                               { return this->stop or !this->tasks.empty(); });
                if (stop and tasks.empty())
                    return;
                task = move(const_cast<task_with_priority &>(tasks.top()));
                tasks.pop();
            }
            available_threads_count--;
            task.task();
            available_threads_count++;
        }
    }

public:
    size_t active_threads(void) { return threads.size(); }
    size_t available(void) { return available_threads_count; }

public:
    ThreadPool(size_t threads_limitation) : stop(false)
    {
        threads.reserve(threads_limitation);
        available_threads_count = 0;
    }

    ~ThreadPool() { stop_threads(); }

    void stop_threads(void)
    {
        if (stop == false)
        {
            {
                unique_lock<mutex> lock(queue_mutex);
                stop = true;
            }
            condition.notify_all();
            for (thread &t : threads)
                t.join();
            available_threads_count = 0;
            threads.clear();
        }
    }

    void restart_threads(void) { stop = false; }

    template <class function_t, class... Args>
    auto enqueue(priority_level level, function_t &&f, Args &&...args) -> future<invoke_result_t<function_t, Args...>>
    {
        typedef invoke_result_t<function_t, Args...> ret_t;
        auto task = make_shared<packaged_task<ret_t()>>(bind(forward<function_t>(f), forward<Args>(args)...));
        future<ret_t> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);
            if (stop)
                throw runtime_error("Thread Pool stopped.");
            tasks.push(task_with_priority([task]()
                                          { (*task)(); }, level));
        }
        if (threads.size() < threads.capacity() and available_threads_count == 0)
        {
            threads.emplace_back(&ThreadPool::executer, this);
            available_threads_count++;
        }
        condition.notify_one();
        return res;
    }
};

void log_write_error_information(const string &err);
void log_write_error_information(string &&err);
void log_write_regular_information(const string &info);
void log_write_regular_information(string &&info);
void log_write_warning_information(const string &info);
void log_write_warning_information(string &&info);

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

class ServerSocket
{
private:
    int listen_sockfd_; // Listening socket file descriptor

public:
    ServerSocket(int port) : listen_sockfd_(-1)
    {
        listen_sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sockfd_ < 0)
        {
            string err = "ServerSocket Error: Failed to create socket - " + string(strerror(errno));
            log_write_error_information(err);
            throw runtime_error(err);
        }

        int opt = 1;
        if (setsockopt(listen_sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            log_write_warning_information("ServerSocket Warning: setsockopt(SO_REUSEADDR) failed");


        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(port);

        if (bind(listen_sockfd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
        {
            close(listen_sockfd_);
            string err = "ServerSocket Error: Failed to bind socket - " + string(strerror(errno));
            log_write_error_information(err);
            throw runtime_error(err);
        }

        if (listen(listen_sockfd_, 10) < 0)
        {
            close(listen_sockfd_);
            string err = "ServerSocket Error: Failed to listen on socket - " + string(strerror(errno));
            log_write_error_information(err);
            throw runtime_error(err);
        }

        stringstream buffer;
        buffer << "ServerSocket: Listening on port " << port << endl;
        log_write_regular_information(buffer.str());
    }

    ~ServerSocket()
    {
        if (listen_sockfd_ >= 0)
        {
            close(listen_sockfd_);
            log_write_regular_information("ServerSocket: Closed listening socket.");
        }
    }

    ServerSocket(const ServerSocket &) = delete;
    ServerSocket &operator=(const ServerSocket &) = delete;

    Socket *accept(void)
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = ::accept(listen_sockfd_, (struct sockaddr *)&client_addr, &client_len);

        if (client_sockfd < 0)
        {
            string err = "ServerSocket Error: Failed to accept connection - " + string(strerror(errno));
            log_write_error_information(err);
            throw runtime_error(err);
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        stringstream buffer;
        buffer << "ServerSocket: Accepted connection from " << client_ip << ":" << client_port << " on fd " << client_sockfd << endl;
        log_write_regular_information(buffer.str());

        return new Socket(client_sockfd);
    }

}; // End class ServerSocket

ThreadStatCode start_compile(void *string_stream);
ThreadStatCode compile_files(void *instruction_buffer);
ThreadStatCode tackle_client(void *client_socket);
ThreadStatCode receive_file(void *string_stream);
ThreadStatCode receive_than_compile(void *string_stream);

#ifndef _GLOBAL_CONSTANT_CPP
extern ThreadPool global_thread_pool;
extern unordered_map<string, function<ThreadStatCode(void *)>> main_thread_process;
extern mutex log_mutex;
extern fstream log_file;
extern bool main_thread_stop_flag;

using priority_level = ThreadPool::priority_level;
#endif

void initialize_main_thread_functions(void);
void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

void initialize(void) throws(runtime_error);
void finalize(void) throws(runtime_error);

#endif
