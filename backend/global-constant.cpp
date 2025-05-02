#define _GLOBAL_CONSTANT_CPP
#include "cloud-compile-backend.hpp"

struct global
{
    global(void)
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

    ~global(void)
    {
        close_log_file();
    }
} automatic;
