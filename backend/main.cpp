#define _MAIN_CPP
#include "cloud-compile-backend.hpp"

int main(int argc, char *argv[])
{
    try
    {
        initialize();
    }
    catch (const runtime_error &err)
    {
        cerr << "Error occurred when initializing:" << endl
             << "    " << err.what() << endl
             << "Program terminated..." << endl;
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++)
    {
        auto search_result = main_thread_process.find(argv[i]);
        if (search_result != main_thread_process.end())
            global_thread_pool.enqueue(priority_level::LOG, search_result->second);
        else
        {
            char error[ERROR_BUFFER_SIZE];
            sprintf(error, "Unkown instruction: %s\nIgnored.\n", argv[i]);
            log_write_error_information(error);
        }
    }

    try
    {
        finalize();
    }
    catch (const runtime_error &err)
    {
        cerr << "Error occurred when finalizing:" << endl
             << "    " << err.what() << endl
             << "Program terminated..." << endl;
        return EXIT_FAILURE;
    }
}

void initialize_main_thread_functions(void)
{
    main_thread_process.insert(make_pair("compile", compile_files_shell));
    main_thread_process.insert(make_pair("clear-log", clear_all_logs));
}
