#ifndef _CLOUD_COMPILE_BACKEND_HPP
#define _CLOUD_COMPILE_BACKEND_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

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

void log_write_error_information(string err);
void log_write_regular_information(string info);

ThreadStatCode compile_files(void *instruction_buffer);
ThreadStatCode clear_all_logs(void);
ThreadStatCode compile_files_shell(void);

#ifndef _GLOBAL_CONSTANT_CPP
extern ThreadPool global_thread_pool;
extern unordered_map<string, function<ThreadStatCode(void)>> main_thread_process;
extern mutex log_mutex;
extern fstream log_file;

using priority_level = ThreadPool::priority_level;
#endif

void initialize_main_thread_functions(void);
void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

void initialize(void) throws(runtime_error);
void finalize(void) throws(runtime_error);

#endif
