#define _GLOBAL_CONSTANT_CPP
#include "cloud-compile-backend.hpp"

ThreadPool global_thread_pool(GLOBAL_THREAD_LIMITATION);

unordered_map<string, function<ThreadStatCode(void)>> main_thread_process;

mutex log_mutex;
fstream log_file;

void initialize(void) throws(runtime_error)
{
    using namespace filesystem;
    initialize_main_thread_functions();

    if (!exists("cpl-log"))
        create_directory("cpl-log");
    make_sure_log_file();
    log_write_regular_information("Program Starts\n");
}

void finalize(void) throws(runtime_error)
{
    global_thread_pool.stop_threads();
    log_write_regular_information("Exit Successfully\n");
    unique_lock<mutex> guard(log_mutex);
    close_log_file();
}
