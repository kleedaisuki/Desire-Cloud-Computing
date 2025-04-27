#define _COMPILE_THREAD_CPP
#include "cloud-compile-backend.hpp"

ThreadStatCode compile_files(void *instruction_list)
{
    using namespace filesystem;
    list<string> &instruction = *static_cast<list<string> *>(instruction_list);

    int stderr_pipe[2];
    if (pipe(stderr_pipe) < 0)
    {
        close(stderr_pipe[0]), close(stderr_pipe[2]);
        return ThreadStatCode ::PIPE_CREATE_FAILED;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(stderr_pipe[0]);
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0)
        {
            close(stderr_pipe[0]), close(stderr_pipe[1]);
            return ThreadStatCode ::REDIRECT_FAILED;
        }

        char **argv = new char *[instruction.size() + 2];
        argv[0] = new char[10]{"g++"};
        auto iter = instruction.begin();
        for (int i = 1; i < instruction.size() + 1; i++, iter++)
        {
            size_t str_length = strlen(iter->c_str()) + 1;
            argv[i] = new char[str_length];
            memcpy(argv[i], iter->c_str(), str_length);
        }
        argv[instruction.size() + 1] = nullptr;

        execvp("g++", argv);

        for (int i = 0; i < instruction.size() + 1; i++)
            delete[] argv[i];
        delete[] argv;
        close(stderr_pipe[1]);
        return ThreadStatCode::SUCCESS;
    }
    else if (pid > 0)
    {
        close(stderr_pipe[1]);
        char err_buffer[ERROR_BUFFER_SIZE + 1];

        stringstream err_info;
        ssize_t check_flag = 0;
        do
        {
            check_flag = read(stderr_pipe[0], err_buffer, ERROR_BUFFER_SIZE);
            err_buffer[check_flag] = '\0';
            err_info << err_buffer;
        } while (check_flag);
        log_write_error_information(err_info.str());

        close(stderr_pipe[0]);
        wait(nullptr);
        return ThreadStatCode ::SUCCESS;
    }
    else
    {
        log_write_error_information("failed to fork...\ncompilation terminated.");
        return ThreadStatCode ::FORK_FAILED;
    }
}

ThreadStatCode start_compile(void *string_stream)
{
    stringstream *stream = static_cast<stringstream *>(string_stream);
    string filename;
    char buffer[FILENAME_BUFFER_SIZE];

    list<string> instructions;

    do
    {
        *stream >> filename;
        sprintf(buffer, "src/%s", filename.c_str());
        instructions.emplace_back(buffer);
    } while (!stream->eof());

    return compile_files(&instructions);
}
