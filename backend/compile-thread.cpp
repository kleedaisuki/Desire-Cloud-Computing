#define _COMPILE_THREAD_CPP
#include "cloud-compile-backend.hpp"

class FdGuard
{
    int fd_ = -1;

public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}
    ~FdGuard()
    {
        if (fd_ != -1)
            close(fd_);
    }
    FdGuard(FdGuard &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ != -1)
                close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;
    int get() const { return fd_; }
    void reset(int fd = -1)
    {
        if (fd_ != -1)
            close(fd_);
        fd_ = fd;
    }
    int release()
    {
        int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }
};

ThreadStatCode compile_files(unique_ptr<vector<string>> instructions)
{
    if (!instructions)
    {
        log_write_error_information("compile_files received null unique_ptr.");
        return ThreadStatCode::INVALID_ARGUMENT;
    }
    if (instructions->empty())
    {
        log_write_error_information("compile_files received empty instruction list.");
        return ThreadStatCode::INVALID_ARGUMENT;
    }

    int stderr_pipe_fds[2];
    if (pipe2(stderr_pipe_fds, O_CLOEXEC) < 0)
    {
        if (errno == ENOSYS)
        {
            if (pipe(stderr_pipe_fds) < 0)
            {
                log_write_error_information("Failed to create pipe: " + string(strerror(errno)));
                return ThreadStatCode::PIPE_CREATE_FAILED;
            }
            if (fcntl(stderr_pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 or
                fcntl(stderr_pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
            {
                log_write_error_information("Failed to set FD_CLOEXEC on pipe: " + string(strerror(errno)));
                close(stderr_pipe_fds[0]);
                close(stderr_pipe_fds[1]);
                return ThreadStatCode::PIPE_CREATE_FAILED;
            }
        }
        else
        {
            log_write_error_information("Failed to create pipe with pipe2: " + string(strerror(errno)));
            return ThreadStatCode::PIPE_CREATE_FAILED;
        }
    }
    FdGuard pipe_read_end(stderr_pipe_fds[0]);
    FdGuard pipe_write_end(stderr_pipe_fds[1]);

    pid_t pid = fork();

    if (pid == 0)
    {
        pipe_read_end.reset();
        if (dup2(pipe_write_end.get(), STDERR_FILENO) < 0)
            _exit(static_cast<int>(ThreadStatCode::REDIRECT_FAILED));
        vector<const char *> argv;
        string command = "g++";
        argv.push_back(command.c_str());
        for (const string &arg : *instructions)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        execvp(argv[0], const_cast<char *const *>(argv.data()));
        cerr << "Child: Failed to execute " << argv[0] << ": " << strerror(errno) << endl;
        _exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        pipe_write_end.reset();
        stringstream error_output_stream;
        char read_buffer[ERROR_BUFFER_SIZE];
        ssize_t bytes_read;
        while ((bytes_read = read(pipe_read_end.get(), read_buffer, sizeof(read_buffer) - 1)) > 0)
        {
            read_buffer[bytes_read] = '\0';
            error_output_stream << read_buffer;
        }
        if (bytes_read < 0)
            log_write_error_information("Error reading from pipe: " + string(strerror(errno)));
        string error_output = error_output_stream.str();
        if (!error_output.empty())
            log_write_error_information("Compiler stderr output:\n" + error_output);

        int child_status;
        if (waitpid(pid, &child_status, 0) == -1)
        {
            log_write_error_information("waitpid failed for PID " + to_string(pid) + ": " + string(strerror(errno)));
            return ThreadStatCode::WAITPID_FAILED;
        }
        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
            {
                return ThreadStatCode::SUCCESS;
            }
            else if (exit_code == static_cast<int>(ThreadStatCode::REDIRECT_FAILED))
            {
                log_write_error_information("Child process failed to redirect stderr for PID " + to_string(pid));
                return ThreadStatCode::REDIRECT_FAILED;
            }
            else
            {
                log_write_error_information("Compilation failed or child exec failed (PID " + to_string(pid) + ") with exit code: " + to_string(exit_code));
                return ThreadStatCode::COMPILE_FAILED;
            }
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            log_write_error_information("Compiler process (PID " + to_string(pid) + ") terminated by signal: " + to_string(term_signal) + " (" + strsignal(term_signal) + ")");
            return ThreadStatCode::PROCESS_SIGNALED;
        }
        else
        {
            log_write_error_information("Compiler process (PID " + to_string(pid) + ") terminated abnormally.");
            return ThreadStatCode::UNKNOWN_ERROR;
        }
    }
    else
    {
        log_write_error_information("Failed to fork process: " + string(strerror(errno)));
        return ThreadStatCode::FORK_FAILED;
    }
}

ThreadStatCode start_compile(unique_ptr<stringstream> stream_owner)
{
    if (!stream_owner)
    {
        log_write_error_information("start_compile received null unique_ptr.");
        return ThreadStatCode::INVALID_ARGUMENT;
    }

    stringstream &stream = *stream_owner;

    auto instructions = make_unique<vector<string>>();
    string filename;
    const string source_directory = "src/";

    while (stream >> filename)
    {
        if (!filename.empty())
        {
            try
            {
                instructions->push_back(source_directory + filename);
            }
            catch (const bad_alloc &e)
            {
                log_write_error_information("Memory allocation failed while building instructions: " + string(e.what()));
                return ThreadStatCode::ALLOCATION_FAILED;
            }
        }
    }

    if (stream.bad())
    {
        log_write_error_information("Error reading from input stringstream.");
        return ThreadStatCode::STREAM_READ_ERROR;
    }

    if (instructions->empty())
    {
        log_write_error_information("No valid filenames found in the input stream.");
        return ThreadStatCode::SUCCESS;
    }

    return compile_files(move(instructions));
}

ThreadStatCode execute_executable(unique_ptr<vector<string>> command_line)
{
    if (!command_line or command_line->empty())
    {
        log_write_error_information("execute_executable received invalid arguments.");
        return ThreadStatCode::INVALID_ARGUMENT;
    }

    int stdout_pipe_fds[2];
    int stderr_pipe_fds[2];
    FdGuard stdout_pipe_read_end, stdout_pipe_write_end;
    FdGuard stderr_pipe_read_end, stderr_pipe_write_end;

    if (pipe2(stdout_pipe_fds, O_CLOEXEC) < 0)
    {
        if (errno == ENOSYS and pipe(stdout_pipe_fds) == 0)
        {
            if (fcntl(stdout_pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 or fcntl(stdout_pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
            {
                log_write_error_information("Failed to set FD_CLOEXEC on stdout pipe: " + string(strerror(errno)));
                close(stdout_pipe_fds[0]);
                close(stdout_pipe_fds[1]);
                return ThreadStatCode::PIPE_CREATE_FAILED;
            }
        }
        else
        {
            log_write_error_information("Failed to create stdout pipe: " + string(strerror(errno)));
            return ThreadStatCode::PIPE_CREATE_FAILED;
        }
    }
    stdout_pipe_read_end.reset(stdout_pipe_fds[0]);
    stdout_pipe_write_end.reset(stdout_pipe_fds[1]);

    if (pipe2(stderr_pipe_fds, O_CLOEXEC) < 0)
    {
        if (errno == ENOSYS and pipe(stderr_pipe_fds) == 0)
        {
            if (fcntl(stderr_pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 or fcntl(stderr_pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
            {
                log_write_error_information("Failed to set FD_CLOEXEC on stderr pipe: " + string(strerror(errno)));
                return ThreadStatCode::PIPE_CREATE_FAILED;
            }
        }
        else
        {
            log_write_error_information("Failed to create stderr pipe: " + string(strerror(errno)));
            return ThreadStatCode::PIPE_CREATE_FAILED;
        }
    }
    stderr_pipe_read_end.reset(stderr_pipe_fds[0]);
    stderr_pipe_write_end.reset(stderr_pipe_fds[1]);

    pid_t pid = fork();

    if (pid == 0)
    {
        stdout_pipe_read_end.reset();
        stderr_pipe_read_end.reset();
        if (dup2(stdout_pipe_write_end.get(), STDOUT_FILENO) < 0)
            _exit(static_cast<int>(ThreadStatCode::REDIRECT_FAILED));
        if (dup2(stderr_pipe_write_end.get(), STDERR_FILENO) < 0)
            _exit(static_cast<int>(ThreadStatCode::REDIRECT_FAILED));
        vector<const char *> argv;
        for (const string &arg : *command_line)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        execvp(command_line->at(0).c_str(), const_cast<char *const *>(argv.data()));
        cerr << "Child: Failed to execute '" << command_line->at(0) << "': " << strerror(errno) << endl;
        _exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        stdout_pipe_write_end.reset();
        stderr_pipe_write_end.reset();

        stringstream stdout_output_stream;
        stringstream stderr_output_stream;

        bool stdout_read_error = false, stderr_read_error = false;

        thread stdout_reader_thread([&](int fd, stringstream &out_stream)
                                    {
                                             char buffer[REGULAR_BUFFER_SIZE];
                                             ssize_t bytes;
                                             while ((bytes = read(fd, buffer, sizeof(buffer) - 1)) > 0)
                                             {
                                                 buffer[bytes] = '\0';
                                                 out_stream << buffer;
                                             }
                                             if (bytes < 0)
                                                 stdout_read_error = true; },
                                    stdout_pipe_read_end.get(),
                                    ref(stdout_output_stream));
        thread stderr_reader_thread([&](int fd, stringstream &err_stream)
                                    {
                                             char buffer[REGULAR_BUFFER_SIZE];
                                             ssize_t bytes;
                                             while ((bytes = read(fd, buffer, sizeof(buffer) - 1)) > 0)
                                             {
                                                 buffer[bytes] = '\0';
                                                 err_stream << buffer;
                                             }
                                            if (bytes < 0)
                                                 stderr_read_error = true; },
                                    stderr_pipe_read_end.get(),
                                    ref(stderr_output_stream));

        stdout_reader_thread.join();
        stderr_reader_thread.join();

        if (stdout_read_error)
            log_write_error_information("Error occurred while reading from child stdout pipe.");
        if (stderr_read_error)
            log_write_error_information("Error occurred while reading from child stderr pipe.");

        string stdout_output = stdout_output_stream.str();
        if (!stdout_output.empty())
        {
            char filename[FILENAME_BUFFER_SIZE];
            time_t now_c = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            int written = snprintf(filename, FILENAME_BUFFER_SIZE, "%s/%li.output", OUT_DIRECTORY, static_cast<long>(now_c));

            fstream outfile;
            outfile.open(filename, ios::app | ios::out | ios::binary);
            if (!outfile.is_open())
                log_write_error_information("Error opening output file: " + string(filename) + '\n');
            outfile << stdout_output;
        }
        string stderr_output = stderr_output_stream.str();
        if (!stderr_output.empty())
            log_write_error_information("Executable stderr output:\n" + stderr_output);

        int child_status;
        if (waitpid(pid, &child_status, 0) == -1)
        {
            log_write_error_information("waitpid failed for PID " + to_string(pid) + ": " + string(strerror(errno)));
            return ThreadStatCode::WAITPID_FAILED;
        }

        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
            {
                return ThreadStatCode::SUCCESS;
            }
            else if (exit_code == static_cast<int>(ThreadStatCode::REDIRECT_FAILED))
                return ThreadStatCode::REDIRECT_FAILED;
            else if (exit_code == static_cast<int>(ThreadStatCode::REDIRECT_FAILED))
                return ThreadStatCode::REDIRECT_FAILED;
            else
            {
                log_write_error_information("Executable failed or child exec failed (PID " + to_string(pid) + ") with exit code: " + to_string(exit_code));
                if (!stderr_output.empty() && stderr_output.find("Child: Failed to execute") != string::npos)
                {
                    return ThreadStatCode::EXEC_FAILED;
                }
                return ThreadStatCode::EXECUTION_FAILED;
            }
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            log_write_error_information("Executable process (PID " + to_string(pid) + ") terminated by signal: " + to_string(term_signal) + " (" + strsignal(term_signal) + ")");
            return ThreadStatCode::PROCESS_SIGNALED;
        }
        else
        {
            log_write_error_information("Executable process (PID " + to_string(pid) + ") terminated abnormally.");
            return ThreadStatCode::UNKNOWN_ERROR;
        }
    }
    else
    {
        log_write_error_information("Failed to fork process: " + string(strerror(errno)));
        return ThreadStatCode::FORK_FAILED;
    }
}
