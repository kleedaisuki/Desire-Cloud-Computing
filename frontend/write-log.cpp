#define _WRITE_LOG_CPP
#include "cloud-compile-frontend.hpp"

static mutex time_mutex;

void make_sure_log_file(void) throws(runtime_error)
{
    if (!log_file.is_open())
    {
        char filename[FILENAME_BUFFER_SIZE];
        time_t now = time(nullptr);
        sprintf(filename, "cpl-log/cpl-front-%li.log", now);
        log_file.open(filename, ios::app | ios::out | ios::binary);
        if (!log_file.is_open())
        {
            string info("Cannot open: ");
            info += filename;
            throw runtime_error(info);
        }
    }
}

void close_log_file(void) throws(runtime_error)
{
    if (log_file.is_open())
    {
        log_file.close();
        if (log_file.is_open())
            throw runtime_error("Log file failed to close");
    }
}

void log_write_error_information(const string &information)
{
    if (information.length())
    {
        using namespace chrono;
        auto point = system_clock::now();
        time_t tmp = system_clock::to_time_t(point);
        tm prepared_time;
        {
            lock_guard guard(time_mutex);
            memcpy(&prepared_time, localtime(&tmp), sizeof(tm));
        }

        unique_lock<mutex> guard(log_mutex);
        log_file << put_time(&prepared_time, "%Y-%m-%d %H:%M:%S ") << "ERROR @thread-" << this_thread::get_id() << endl
                 << information << endl;
    }
}

void log_write_error_information(string &&information)
{
    if (information.length())
    {
        using namespace chrono;
        auto point = system_clock::now();
        time_t tmp = system_clock::to_time_t(point);
        tm prepared_time;
        {
            lock_guard guard(time_mutex);
            memcpy(&prepared_time, localtime(&tmp), sizeof(tm));
        }

        unique_lock<mutex> guard(log_mutex);
        log_file << put_time(&prepared_time, "%Y-%m-%d %H:%M:%S ") << "ERROR @thread-" << this_thread::get_id() << endl
                 << information << endl;
    }
}

void log_write_regular_information(const string &information)
{
    if (information.length())
    {
        using namespace chrono;
        auto point = system_clock::now();
        time_t tmp = system_clock::to_time_t(point);
        tm prepared_time;
        {
            lock_guard guard(time_mutex);
            memcpy(&prepared_time, localtime(&tmp), sizeof(tm));
        }

        unique_lock<mutex> guard(log_mutex);
        log_file << put_time(&prepared_time, "%Y-%m-%d %H:%M:%S ") << "INFO @thread-" << this_thread::get_id() << endl
                 << information << endl;
    }
}

void log_write_regular_information(string &&information)
{
    if (information.length())
    {
        using namespace chrono;
        auto point = system_clock::now();
        time_t tmp = system_clock::to_time_t(point);
        tm prepared_time;
        {
            lock_guard guard(time_mutex);
            memcpy(&prepared_time, localtime(&tmp), sizeof(tm));
        }

        unique_lock<mutex> guard(log_mutex);
        log_file << put_time(&prepared_time, "%Y-%m-%d %H:%M:%S ") << "INFO @thread-" << this_thread::get_id() << endl
                 << information << endl;
    }
}
