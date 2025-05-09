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

#ifndef _BACKEND_NETWORK_HPP
#define _BACKEND_NETWORK_HPP

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <system_error>
#include <cstring>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <limits>
#include <filesystem>
#include <future>
#include <list>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/uio.h>
#include <cerrno>

using namespace std;

void log_write_error_information(const string &err);
void log_write_error_information(string &&err);
void log_write_regular_information(const string &info);
void log_write_regular_information(string &&info);
void log_write_warning_information(const string &info);
void log_write_warning_information(string &&info);

class ThreadPool
{
public:
    static ThreadPool &instance()
    {
        static ThreadPool global_thread_pool;
        return global_thread_pool;
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    ~ThreadPool()
    {
        {
            lock_guard lk{mtx_};
            stop_ = true;
        }

        cv_.notify_all();

        for (thread &w : workers_)
            if (w.joinable())
                w.join();
        log_write_regular_information("thread pool closed");
    }

    template <typename F, typename... Args>
        requires invocable<F, Args...>
    auto enqueue(F &&f, Args &&...args)
    {
        return enqueue_internal(0, forward<F>(f), forward<Args>(args)...);
    }

    template <typename F, typename... Args>
        requires invocable<F, Args...>
    auto enqueue(int priority, F &&f, Args &&...args)
    {
        return enqueue_internal(priority, forward<F>(f), forward<Args>(args)...);
    }

    explicit ThreadPool(size_t max_threads_param = thread::hardware_concurrency())
        : max_threads_{max_threads_param == 0 ? 1 : max_threads_param},
          stop_{false},
          seq_{0},
          idle_threads_{0}
    {

        log_write_regular_information("thread pool initialzed with maximum set:" + to_string(max_threads_));
        workers_.reserve(max_threads_);
    }

private:
    using Task = function<void()>;

    struct TaskWrapper
    {
        int priority;
        size_t seq;
        Task func;

        TaskWrapper() : priority(0), seq(0), func(nullptr) {}
        TaskWrapper(int p, size_t s, Task f) : priority(p), seq(s), func(move(f)) {}
    };

    struct Compare
    {
        bool operator()(const TaskWrapper &a, const TaskWrapper &b) const
        {
            if (a.priority != b.priority)
                return a.priority < b.priority;
            return a.seq > b.seq;
        }
    };

    template <typename F, typename... Args>
    auto enqueue_internal(int priority, F &&f, Args &&...args)
    {
        using R = invoke_result_t<F, Args...>;
        auto taskPtr = make_shared<packaged_task<R()>>(
            bind(forward<F>(f), forward<Args>(args)...));
        future<R> future = taskPtr->get_future();

        {
            lock_guard lk{mtx_};

            if (stop_)
            {
                log_write_error_information("task pushed to stopped thread pool");
                return std::future<R>();
            }

            tasks_.emplace(TaskWrapper{priority, seq_++, [taskPtr]()
                                       { (*taskPtr)(); }});
            log_write_regular_information("task pushed, priority:" + to_string(priority) + ", sequence code:" + to_string(seq_.load() - 1));

            if (idle_threads_ == 0 and workers_.size() < max_threads_)
                workers_.emplace_back([this]
                                      { worker_thread(); });
        }

        cv_.notify_one();
        return future;
    }

    void worker_thread(void)
    {
        log_write_regular_information("thread pool: worker start");
        while (true)
        {
            TaskWrapper task_to_run;

            {
                unique_lock lk{mtx_};

                idle_threads_++;
                cv_.wait(lk, [this]
                         { return stop_ || !tasks_.empty(); });
                idle_threads_--;

                if (stop_ and tasks_.empty())
                {
                    log_write_regular_information("thread pool: worker exited");
                    return;
                }

                task_to_run = move(const_cast<TaskWrapper &>(tasks_.top()));
                tasks_.pop();
            }

            try
            {
                if (task_to_run.func)
                    task_to_run.func();
                else
                    log_write_error_information("thread pool: worker: ignored empty task");
            }
            catch (const exception &e)
            {
                log_write_error_information("thread pool: worker: error occurred:" + string(e.what()));
            }
            catch (...)
            {
                log_write_error_information("thread pool: worker: unkown exception occurred");
            }
        }
    }

    vector<thread> workers_;
    priority_queue<TaskWrapper, vector<TaskWrapper>, Compare> tasks_;

    mutex mtx_;
    condition_variable cv_;

    atomic<bool> stop_;
    atomic<size_t> seq_;

    atomic<size_t> idle_threads_;
    const size_t max_threads_;
};

inline string errno_to_string(int err_no) { return string(strerror(err_no)); }

class ClientSocket
{
    class Buffer
    {
    public:
        static constexpr size_t kInitialSize = 4096;
        static constexpr size_t kPrependSize = 8;
        static constexpr size_t kMaxFrameSize = 64 * 1024 * 1024; // 64 MiB

        explicit Buffer(size_t initial_size = kInitialSize)
            : buffer_(kPrependSize + initial_size),
              read_index_(kPrependSize),
              write_index_(kPrependSize) {}

        void swap(Buffer &rhs) noexcept
        {
            buffer_.swap(rhs.buffer_);
            std::swap(read_index_, rhs.read_index_);
            std::swap(write_index_, rhs.write_index_);
        }

        size_t readable_bytes() const noexcept { return write_index_ - read_index_; }
        size_t writable_bytes() const noexcept { return buffer_.size() - write_index_; }
        size_t prependable_bytes() const noexcept { return read_index_; }

        const char *peek() const noexcept { return begin() + read_index_; }
        char *begin_write() noexcept { return begin() + write_index_; }
        const char *begin_write() const noexcept { return begin() + write_index_; }

        void has_written(size_t len) noexcept { write_index_ += len; }

        void ensure_writable_bytes(size_t len)
        {
            if (writable_bytes() < len)
                make_space(len);
        }

        void append(const void *data, size_t len)
        {
            ensure_writable_bytes(len);
            memcpy(begin_write(), data, len);
            has_written(len);
        }
        void append(const string &str) { append(str.data(), str.length()); }

        void prepend(const void *data, size_t len)
        {
            if (len > prependable_bytes())
            {
                log_write_error_information("Prepend length exceeds prependable space");
                return;
            }
            read_index_ -= len;
            const char *d = static_cast<const char *>(data);
            memcpy(begin() + read_index_, d, len);
        }

        void retrieve(size_t len) noexcept
        {
            if (len < readable_bytes())
                read_index_ += len;
            else
                retrieve_all();
        }
        void retrieve_all() noexcept { read_index_ = write_index_ = kPrependSize; }

        string retrieve_as_string(size_t len)
        {
            len = min(len, readable_bytes());
            string result(peek(), len);
            retrieve(len);
            return result;
        }
        string retrieve_all_as_string() { return retrieve_as_string(readable_bytes()); }

        ssize_t read_fd(int fd, int *saved_errno);

    private:
        char *begin() noexcept { return buffer_.data(); }
        const char *begin() const noexcept { return buffer_.data(); }

        void make_space(size_t len)
        {
            if (prependable_bytes() + writable_bytes() < len + kPrependSize)
                buffer_.resize(write_index_ + len);
            else
            {
                size_t readable = readable_bytes();
                memmove(begin() + kPrependSize, peek(), readable);
                read_index_ = kPrependSize;
                write_index_ = read_index_ + readable;
            }
        }

    private:
        vector<char> buffer_;
        size_t read_index_;
        size_t write_index_;
    };

    class ConnectionManager;
    class Sender;
    class Receiver;
    class MessageHandler;

public:
    using Handler = function<void(const string &payload)>;
    using ConnectionCallback = function<void(bool connected)>;
    using ErrorCallback = function<void(const string &error_msg)>;

private:
    string server_ip_;
    uint16_t server_port_;
    atomic<int> sockfd_;
    atomic<bool> is_connected_;
    atomic<bool> stop_requested_;

    ConnectionCallback connection_cb_;
    ErrorCallback error_cb_;

    ThreadPool &thread_pool_;

    unique_ptr<ConnectionManager> connection_manager_;
    unique_ptr<Sender> sender_;
    unique_ptr<Receiver> receiver_;
    unique_ptr<MessageHandler> message_handler_;

    thread send_thread_;
    thread recv_thread_;

    mutex connection_mutex_;

public:
    ClientSocket(string server_ip, uint16_t server_port);
    ~ClientSocket();

    ClientSocket(const ClientSocket &) = delete;
    ClientSocket &operator=(const ClientSocket &) = delete;
    ClientSocket(ClientSocket &&) = delete;
    ClientSocket &operator=(ClientSocket &&) = delete;

    bool connect();
    void disconnect();
    bool is_connected() const { return is_connected_.load(memory_order_relaxed); }

    bool send_message(const string &tag, string_view payload);
    bool send_message(const string &tag, unique_ptr<char[]> buffer, size_t buflen);
    bool send_text(const string &tag, const string &text_payload);
    bool send_binary(const string &tag, const vector<char> &binary_payload);
    bool send_file(const string &tag, const string &file_path, size_t chunk_size = 64 * 1024);

    void register_handler(const string &tag, Handler handler);
    void register_default_handler(Handler handler);
    void register_connection_callback(ConnectionCallback cb);
    void register_error_callback(ErrorCallback cb);

private:
    friend class ConnectionManager;
    friend class Sender;
    friend class Receiver;
    friend class MessageHandler;

    void trigger_error_callback_internal(const string &error_msg);
    void trigger_connection_callback_internal(bool connected);
    void request_disconnect_async_internal(const string &reason);
    bool start_io_threads();
    void stop_and_join_io_threads();
    bool connect_internal();
    void disconnect_internal();

    class ConnectionManager
    {
    private:
        ClientSocket &owner_;

    public:
        explicit ConnectionManager(ClientSocket &owner) : owner_(owner) {}

        int try_connect(void);

        void close_socket(int &sockfd_ref)
        {
            if (sockfd_ref != -1)
            {
                log_write_regular_information("socket closed.");
                shutdown(sockfd_ref, SHUT_RDWR);
                close(sockfd_ref);
                sockfd_ref = -1;
            }
        }
    };

    class Sender
    {
    private:
        ClientSocket &owner_;
        queue<tuple<unique_ptr<char[]>, size_t>> send_queue_;
        mutex send_mutex_;
        condition_variable send_cv_;

    public:
        explicit Sender(ClientSocket &owner) : owner_(owner) {}

        // void enqueue_message(vector<char> message)
        // {
        //     unique_ptr<char[]> msg = make_unique<char[]>(message.size());
        //     memcpy(msg.get(), message.data(), message.size());
        //     {
        //         lock_guard<mutex> lock(send_mutex_);
        //         send_queue_.push({move(msg), message.size()});
        //     }
        //     send_cv_.notify_one();
        // }

        void enqueue_message(unique_ptr<char[]> message, size_t msglen)
        {
            {
                lock_guard<mutex> lock(send_mutex_);
                send_queue_.push({move(message), msglen});
            }
            send_cv_.notify_one();
        }

        void clear_queue()
        {
            lock_guard<mutex> lock(send_mutex_);
            queue<tuple<unique_ptr<char[]>, size_t>>{}.swap(send_queue_);
        }

        void send_loop();

        void notify_sender() { send_cv_.notify_one(); }

    private:
        bool send_all_internal(const char *data, size_t len);
    };

    class Receiver
    {
    private:
        ClientSocket &owner_;
        Buffer recv_buffer_;

    public:
        explicit Receiver(ClientSocket &owner) : owner_(owner) {}

        void recv_loop(void);

        void clear_buffer() { recv_buffer_.retrieve_all(); }
    };

    class MessageHandler
    {
    private:
        ClientSocket &owner_;
        shared_mutex handler_rw_mutex_;
        unordered_map<string, Handler> handlers_;
        Handler default_handler_;

    public:
        explicit MessageHandler(ClientSocket &owner) : owner_(owner) {}
        void register_handler(const string &tag, Handler handler)
        {
            if (!handler)
            {
                log_write_warning_information("Attempted to register a null handler for tag: " + tag);
                return;
            }
            unique_lock lock(handler_rw_mutex_);
            handlers_.insert_or_assign(tag, handler);
            log_write_regular_information("Registered handler for tag: " + tag);
        }
        void register_default_handler(Handler handler)
        {
            if (!handler)
            {
                log_write_warning_information("Attempted to register a null default handler.");
                return;
            }
            unique_lock lock(handler_rw_mutex_);
            default_handler_ = move(handler);
            log_write_regular_information("Registered default handler.");
        }

        void process_received_data(Buffer &recv_buffer);
    };
};

inline ssize_t ClientSocket::Buffer::read_fd(int fd, int *saved_errno)
{
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writable_bytes();
    vec[0].iov_base = begin_write();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
        *saved_errno = errno;
    else if (static_cast<size_t>(n) <= writable)
        has_written(n);
    else
    {
        write_index_ = buffer_.size();
        append(extrabuf, static_cast<size_t>(n) - writable);
    }
    return n;
}

#endif
