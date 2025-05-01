#define _GLOBAL_CONSTANT_CPP
#include "cloud-compile-backend.hpp"

mutex log_mutex;
fstream log_file;

ThreadPool global_thread_pool(15);

void initialize(void) throws(runtime_error)
{
    using namespace filesystem;

    if (!exists("cpl-log"))
        create_directory("cpl-log");
    if (!exists("bin"))
        create_directory("bin");
    if (!exists("src"))
        create_directory("src");
    if (!exists("out"))
        create_directory("out");

    make_sure_log_file();
    log_write_regular_information("Program Starts");
}

void finalize(void) throws(runtime_error)
{
    log_write_regular_information("Exit Successfully\n");
    unique_lock<mutex> guard(log_mutex);
    close_log_file();
}

