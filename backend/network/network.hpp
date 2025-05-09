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
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/uio.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstdio>

using namespace std;

void log_write_error_information(const string &err);
void log_write_error_information(string &&err);
void log_write_regular_information(const string &info);
void log_write_regular_information(string &&info);
void log_write_warning_information(const string &info);
void log_write_warning_information(string &&info);

inline string errno_to_string(int err_no) { return string(strerror(err_no)); }

namespace util
{
    inline int set_non_blocking(int fd)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            log_write_error_information("fcntl(F_GETFL) failed for fd " + to_string(fd) + ": " + errno_to_string(errno));
            return -1;
        }
        if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            log_write_error_information("fcntl(F_SETFL, O_NONBLOCK) failed for fd " + to_string(fd) + ": " + errno_to_string(errno));
            return -1;
        }
        return 0;
    }

    inline void fatal_perror(const string &msg)
    {
        string error_msg = msg + ": " + errno_to_string(errno);
        log_write_error_information("FATAL ERROR: " + error_msg);
        exit(EXIT_FAILURE);
    }
}

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
        TaskWrapper(int p, size_t s, Task f) : priority(p), seq(s), func(move(f)) {} // 使用移动语义接管 Task
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

class Buffer
{
public:
    static constexpr size_t kCheapPrepend = 8;
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initial_size = kInitialSize)
        : buffer_(kCheapPrepend + initial_size),
          reader_index_(kCheapPrepend),
          writer_index_(kCheapPrepend) {}

    size_t readable_bytes() const noexcept { return writer_index_ - reader_index_; }
    size_t writable_bytes() const noexcept { return buffer_.size() - writer_index_; }
    size_t prependable_bytes() const noexcept { return reader_index_; }

    const char *peek() const noexcept { return begin() + reader_index_; }
    char *begin_write() noexcept { return begin() + writer_index_; }
    const char *begin_write() const noexcept { return begin() + writer_index_; }

    void retrieve(size_t len) noexcept
    {
        assert(len <= readable_bytes());
        if (len < readable_bytes())
            reader_index_ += len;
        else
            retrieve_all();
    }

    void retrieve_all() noexcept
    {
        reader_index_ = kCheapPrepend;
        writer_index_ = kCheapPrepend;
    }

    string retrieve_all_as_string()
    {
        return retrieve_as_string(readable_bytes());
    }

    string retrieve_as_string(size_t len)
    {
        assert(len <= readable_bytes());
        string result(peek(), len);
        retrieve(len);
        return result;
    }

    void append(const char *data, size_t len)
    {
        ensure_writable_bytes(len);
        memcpy(begin_write(), data, len);
        has_written(len);
    }

    void append(string_view sv)
    {
        append(sv.data(), sv.size());
    }

    void has_written(size_t len) noexcept
    {
        assert(len <= writable_bytes());
        writer_index_ += len;
    }

    void ensure_writable_bytes(size_t len)
    {
        if (writable_bytes() < len)
            make_space(len);
        assert(writable_bytes() >= len);
    }

    ssize_t read_fd(int fd, int *saved_errno);

private:
    char *begin() noexcept { return buffer_.data(); }
    const char *begin() const noexcept { return buffer_.data(); }

    void make_space(size_t len);

    vector<char> buffer_;
    size_t reader_index_;
    size_t writer_index_;
};

namespace net
{
    class Socket
    {
    public:
        explicit Socket(int fd = -1) noexcept : fd_{fd} {}
        ~Socket() { close(); }

        Socket(const Socket &) = delete;
        Socket &operator=(const Socket &) = delete;

        Socket(Socket &&other) noexcept : fd_{other.fd_} { other.fd_ = -1; }

        Socket &operator=(Socket &&other) noexcept
        {
            if (this != &other)
            {
                close();
                fd_ = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }

        int fd() const noexcept { return fd_; }
        int release() noexcept
        {
            int tmp = fd_;
            fd_ = -1;
            return tmp;
        }

        void close() noexcept
        {
            if (fd_ != -1)
            {
                log_write_regular_information("Closing socket fd: " + to_string(fd_));
                if (::close(fd_) == -1)
                    log_write_error_information("Error closing socket fd " + to_string(fd_) + ": " + errno_to_string(errno));
                fd_ = -1;
            }
        }

    private:
        int fd_;
    };

    class Channel;
    class TcpConnection;
    class TcpServer;
    using TcpConnectionPtr = shared_ptr<TcpConnection>;

    class EventLoop
    {
    public:
        using Functor = function<void()>;

        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;

        void loop();
        void quit();
        void run_in_loop(Functor f);
        void queue_in_loop(Functor f);

        void update_channel(Channel *channel);
        void remove_channel(Channel *channel);
        bool has_channel(Channel *channel);

        void assert_in_loop_thread()
        {
            if (!is_in_loop_thread())
                abort_not_in_loop_thread();
        }
        bool is_in_loop_thread() const { return thread_id_ == this_thread::get_id(); }

    private:
        void handle_read();
        void do_pending_functors();
        void abort_not_in_loop_thread();
        void wakeup();

        using ChannelMap = unordered_map<int, Channel *>;

        atomic<bool> looping_;
        atomic<bool> quit_;
        atomic<bool> event_handling_;
        atomic<bool> calling_pending_functors_;

        const thread::id thread_id_;
        Socket epoll_fd_;
        Socket wakeup_fd_;
        unique_ptr<Channel> wakeup_channel_;

        ChannelMap channels_;
        static constexpr int kMaxEvents = 64;
        vector<struct epoll_event> active_events_;

        mutex mutex_;
        vector<Functor> pending_functors_;
    };

    class Channel
    {
    public:
        using EventCallback = function<void()>;

        explicit Channel(EventLoop *loop, int fd) : loop_{loop}, fd_{fd} {}
        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;

        void handle_event();

        Channel &on_read(EventCallback cb)
        {
            read_cb_ = move(cb);
            return *this;
        }
        Channel &on_write(EventCallback cb)
        {
            write_cb_ = move(cb);
            return *this;
        }
        Channel &on_error(EventCallback cb)
        {
            error_cb_ = move(cb);
            return *this;
        }

        int fd() const noexcept { return fd_; }
        uint32_t events() const noexcept { return events_; }
        void set_events(uint32_t ev) noexcept { events_ = ev; }
        void set_revents(uint32_t rev) noexcept { revents_ = rev; }

        void enable_reading()
        {
            events_ |= EPOLLIN | EPOLLPRI;
            update();
        }
        void disable_reading()
        {
            events_ &= ~(EPOLLIN | EPOLLPRI);
            update();
        }

        void enable_writing()
        {
            events_ |= EPOLLOUT;
            update();
        }

        void disable_writing()
        {
            events_ &= ~EPOLLOUT;
            update();
        }

        void disable_all()
        {
            events_ = 0;
            update();
        }

        bool is_none_event() const noexcept { return events_ == 0; }
        bool is_writing() const noexcept { return events_ & EPOLLOUT; }
        bool is_reading() const noexcept { return events_ & EPOLLIN; }

        EventLoop *owner_loop() const noexcept { return loop_; }
        void remove()
        {
            assert(is_none_event());
            added_to_loop_ = false;
            loop_->remove_channel(this);
        }

        void tie(const shared_ptr<void> &obj)
        {
            tie_ = obj;
            tied_ = true;
        }

    private:
        void update()
        {
            added_to_loop_ = true;
            loop_->update_channel(this);
        }

        EventLoop *loop_;
        const int fd_;
        uint32_t events_{0};
        uint32_t revents_{0};

        EventCallback read_cb_{};
        EventCallback write_cb_{};
        EventCallback error_cb_{};

        weak_ptr<void> tie_;
        bool tied_{false};
        bool added_to_loop_{false};
    };

    class TcpConnection : public enable_shared_from_this<TcpConnection>
    {
    public:
        using MessageCallback = function<tuple<unique_ptr<char[]>, size_t>(const TcpConnectionPtr &, Buffer *)>;
        using ConnectionCallback = function<void(const TcpConnectionPtr &)>;
        using WriteCompleteCallback = function<void(const TcpConnectionPtr &)>;
        using HighWaterMarkCallback = function<void(const TcpConnectionPtr &, size_t)>;
        using CloseCallback = function<void(const TcpConnectionPtr &)>;

        TcpConnection(EventLoop *loop,
                      string name,
                      int sockfd,
                      const sockaddr_in &local_addr,
                      const sockaddr_in &peer_addr);
        ~TcpConnection();

        void set_connection_callback(const ConnectionCallback &cb) { connection_cb_ = cb; }
        void set_message_callback(const MessageCallback &cb) { message_cb_ = cb; }
        void set_write_complete_callback(const WriteCompleteCallback &cb) { write_complete_cb_ = cb; }
        void set_high_water_mark_callback(const HighWaterMarkCallback &cb, size_t mark)
        {
            high_water_mark_cb_ = cb;
            high_water_mark_ = mark;
        }
        void set_close_callback(CloseCallback cb) { close_cb_ = move(cb); }

        void send(unique_ptr<char[]> message, size_t buflen);
        void send(string_view message);
        void send(Buffer *buf);
        void shutdown();
        void force_close();

        bool connected() const { return state_ == State::kConnected; }
        bool disconnected() const { return state_ == State::kDisconnected; }

        EventLoop *get_loop() const { return loop_; }
        const string &name() const { return name_; }
        const sockaddr_in &local_address() const { return local_addr_; }
        const sockaddr_in &peer_address() const { return peer_addr_; }

        void connect_established();
        void connect_destroyed();

    private:
        enum class State
        {
            kDisconnected,
            kConnecting,
            kConnected,
            kDisconnecting
        };
        void set_state(State s) { state_ = s; }

        void handle_read();
        void handle_write();
        void handle_close();
        void handle_error();

        void send_in_loop(const void *data, size_t len);
        void send_in_loop(string_view message);

        void shutdown_in_loop();
        void force_close_in_loop();

        EventLoop *loop_;
        string name_;
        State state_;
        atomic<bool> reading_;

        Socket socket_;
        unique_ptr<Channel> channel_;

        const sockaddr_in local_addr_;
        const sockaddr_in peer_addr_;

        ConnectionCallback connection_cb_;
        MessageCallback message_cb_;
        WriteCompleteCallback write_complete_cb_;
        HighWaterMarkCallback high_water_mark_cb_;
        CloseCallback close_cb_;

        size_t high_water_mark_;
        Buffer input_buffer_;
        Buffer output_buffer_;
    };

    class Acceptor
    {
    public:
        using NewConnectionCallback = function<void(int sockfd, const sockaddr_in &peer_addr)>;

        Acceptor(EventLoop *loop, uint16_t port, bool reuse_port);
        ~Acceptor();

        Acceptor(const Acceptor &) = delete;
        Acceptor &operator=(const Acceptor &) = delete;

        void set_new_connection_callback(NewConnectionCallback cb)
        {
            new_connection_cb_ = move(cb);
        }

        void listen();
        bool listening() const { return listening_; }

    private:
        void handle_read();

        EventLoop *loop_;
        Socket accept_socket_;
        Channel accept_channel_;
        NewConnectionCallback new_connection_cb_;
        bool listening_;
        int idle_fd_;
    };

    class TcpServer
    {
    public:
        using HandlerTag = string;
        using Handler = function<string(const TcpConnectionPtr &, Buffer *)>;
        using ProtocolHandlerPair = pair<HandlerTag, string>;
        using ProtocolHandler = function<ProtocolHandlerPair(const TcpConnectionPtr &conn,
                                                                  const string &tag,
                                                                  string_view payload)>;

        TcpServer(EventLoop *loop, uint16_t port, string name = "MyTcpServer", bool reuse_port = true);
        ~TcpServer();

        TcpServer(const TcpServer &) = delete;
        TcpServer &operator=(const TcpServer &) = delete;
        TcpServer(TcpServer &&) = delete;
        TcpServer &operator=(TcpServer &&) = delete;

        void register_protocol_handler(const string &tag, ProtocolHandler cb);
        void set_default_protocol_handler(ProtocolHandler cb);
        void register_handler(HandlerTag tag, Handler cb);
        void set_default_handler(Handler cb);
        void set_connection_callback(const TcpConnection::ConnectionCallback &cb);
        void set_write_complete_callback(const TcpConnection::WriteCompleteCallback &cb);
        void start();
        EventLoop *get_loop() const { return loop_; }
        const string &name() const { return name_; }
        static string package_message(const string &tag, string_view payload);

    private:
        bool attempt_protocol_processing(const TcpConnectionPtr &conn, Buffer *buf);
        void execute_protocol_handler(const ProtocolHandler &handler, const TcpConnectionPtr &conn, Buffer *buf, const string &tag, size_t header_len, uint32_t payload_len);
        bool execute_legacy_handler_for_tag(const string &tag, const TcpConnectionPtr &conn, Buffer *buf);
        void execute_default_protocol_handler(const ProtocolHandler &handler, const TcpConnectionPtr &conn, Buffer *buf, const string &tag, size_t header_len, uint32_t payload_len);
        bool process_legacy_fallback(const TcpConnectionPtr &conn, Buffer *buf, size_t initial_readable);
        tuple<unique_ptr<char[]>, size_t> on_message(const TcpConnectionPtr &conn, Buffer *buf);
        void new_connection(int sockfd, const sockaddr_in &peer_addr);
        void remove_connection(const TcpConnectionPtr &conn);
        void remove_connection_in_loop(const TcpConnectionPtr &conn);

        EventLoop *loop_;
        const string name_;

        unique_ptr<Acceptor> acceptor_;
        bool started_;
        int next_conn_id_;

        TcpConnection::ConnectionCallback connection_cb_;
        TcpConnection::WriteCompleteCallback write_complete_cb_;

        unordered_map<string, ProtocolHandler> protocol_handlers_;
        ProtocolHandler default_protocol_handler_;
        unordered_map<HandlerTag, Handler> handlers_;
        Handler default_handler_;

        unordered_map<string, TcpConnectionPtr> connections_;

        static constexpr size_t kMaxPayloadSize = 64 * 1024 * 1024; // 64 MiB
    };
}

#endif
