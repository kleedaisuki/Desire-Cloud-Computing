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

#define _WRITE_LOG_CPP
#include "cloud-compile-backend.hpp"

class Logger
{
public:
    static Logger &getInstance()
    {
        static Logger instance;
        if (!instance.is_initialized_)
            throw runtime_error("Logger initialization failed. Check previous errors (e.g., stderr).");
        return instance;
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    void enqueueLog(string &&log_entry)
    {
        if (!is_initialized_ or shutdown_requested_.load(memory_order_relaxed))
            return;
        {
            lock_guard<mutex> lock(queue_mutex_);
            log_queue_.push(move(log_entry));
        }
        cv_.notify_one();
    }

    void flush()
    {
        if (!is_initialized_ or shutdown_requested_.load(memory_order_relaxed))
            return;
        cv_.notify_one();
    }

    bool isFileOpen() const { return is_initialized_; }

private:
    Logger() : shutdown_requested_(false), is_initialized_(false)
    {
        try
        {
            char filename[FILENAME_BUFFER_SIZE];
            time_t now_c = chrono::system_clock::to_time_t(chrono::system_clock::now());
            int written = snprintf(filename, FILENAME_BUFFER_SIZE, "%s/cpl-back-%ld.log", LOG_DIRECTORY, static_cast<long>(now_c));

            log_file_.open(filename, ios::app | ios::out | ios::binary);
            if (!log_file_.is_open())
            {
                cerr << "Error opening log file: " << filename << endl;
                throw runtime_error(string("Cannot open log file: ") + filename);
            }

            writer_thread_ = thread(&Logger::worker, this);
            is_initialized_ = true;
        }
        catch (const exception &e)
        {
            cerr << "Logger initialization failed: " << e.what() << endl;
            if (log_file_.is_open())
                log_file_.close();
            shutdown_requested_ = true;
            if (writer_thread_.joinable())
            {
                cv_.notify_all();
                writer_thread_.join();
            }
            is_initialized_ = false;
        }
        catch (...)
        {
            cerr << "Logger initialization failed due to an unknown exception." << endl;
            if (log_file_.is_open())
                log_file_.close();
            shutdown_requested_ = true;
            if (writer_thread_.joinable())
            {
                cv_.notify_all();
                writer_thread_.join();
            }
            is_initialized_ = false;
        }
    }

    ~Logger()
    {
        if (is_initialized_ and !shutdown_requested_.exchange(true))
        {
            cv_.notify_all();
            if (writer_thread_.joinable())
                writer_thread_.join();
        }
        else if (writer_thread_.joinable())
        {
            if (!shutdown_requested_.load())
            {
                shutdown_requested_ = true;
                cv_.notify_all();
            }
            writer_thread_.join();
        }

        if (log_file_.is_open())
        {
            log_file_.flush();
            log_file_.close();
        }
    }

    void worker()
    {
        queue<string> local_queue;

        while (true)
        {
            unique_lock<mutex> lock(queue_mutex_);

            cv_.wait(lock, [this]
                     { return !log_queue_.empty() or shutdown_requested_.load(memory_order_relaxed); });

            if (shutdown_requested_.load(memory_order_relaxed) and log_queue_.empty())
            {
                lock.unlock();
                break;
            }

            if (!log_queue_.empty())
                local_queue.swap(log_queue_);

            lock.unlock();

            while (!local_queue.empty())
            {
                if (log_file_.good())
                    log_file_ << local_queue.front() << '\n';
                else
                    cerr << "Error writing to log file. Further logs might be lost." << endl;
                local_queue.pop();
            }

            if (log_file_.good())
                log_file_.flush();
        }

        if (log_file_.good())
            log_file_.flush();
    }

    ofstream log_file_;
    queue<string> log_queue_;
    mutex queue_mutex_;
    condition_variable cv_;
    thread writer_thread_;
    atomic<bool> shutdown_requested_;
    atomic<bool> is_initialized_;
};

inline string formatLogMessage(const char *level, const string &information)
{
    using namespace chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;
    time_t tt = system_clock::to_time_t(now);

    struct tm prepared_time;
    if (localtime_r(&tt, &prepared_time) == nullptr)
    {
        ostringstream error_oss;
        error_oss << "[TIME_ERR] " << level
                  << information;
        return error_oss.str();
    }

    ostringstream oss;
    oss << put_time(&prepared_time, "%Y-%m-%d %H:%M:%S");
    oss << '.' << setfill('0') << setw(6) << ms.count();
    oss << ' ' << level;
    oss << information;

    return oss.str();
}

void make_sure_log_file()
{
    try
    {
        Logger::getInstance();
    }
    catch (const runtime_error &e)
    {
        throw;
    }
    catch (const exception &e)
    {
        throw runtime_error(string("Logger initialization check failed: ") + e.what());
    }
    catch (...)
    {
        throw runtime_error("Unknown error during logger initialization check.");
    }
}

void close_log_file()
{
    try
    {
        Logger::getInstance().flush();
    }
    catch (const runtime_error &e)
    {
        throw;
    }
    catch (const exception &e)
    {
        throw runtime_error(string("Log file flush failed: ") + e.what());
    }
    catch (...)
    {
        throw runtime_error("Unknown error during log file flush.");
    }
}

void log_write_error_information(const string &information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("ERROR ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write ERROR log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write ERROR log." << endl;
        }
    }
}

void log_write_error_information(string &&information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("ERROR ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write ERROR log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write ERROR log." << endl;
        }
    }
}

void log_write_regular_information(const string &information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("INFO  ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write INFO log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write INFO log." << endl;
        }
    }
}

void log_write_regular_information(string &&information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("INFO  ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write INFO log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write INFO log." << endl;
        }
    }
}

void log_write_warning_information(const string &information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("WARN  ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write WARN log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write WARN log." << endl;
        }
    }
}

void log_write_warning_information(string &&information)
{
    if (!information.empty())
    {
        try
        {
            Logger::getInstance().enqueueLog(formatLogMessage("WARN  ", information));
        }
        catch (const runtime_error &e)
        {
            cerr << "LOGGER NOT INITIALIZED: Failed to write WARN log: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "LOGGER UNKNOWN ERROR: Failed to write WARN log." << endl;
        }
    }
}
