#ifndef _BACKEND_DEFS_HPP
#define _BACKEND_DEFS_HPP

enum class ThreadStatCode
{
    SUCCESS,
    FILE_NOT_FOUND,
    PIPE_CREATE_FAILED,
    REDIRECT_FAILED,
    FORK_FAILED,
};

#define FILENAME_BUFFER_SIZE 260
#define ERROR_BUFFER_SIZE 500

#define throws(name)

#define GLOBAL_THREAD_LIMITATION 20

#endif
