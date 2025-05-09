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
            if (close(fd_) == -1)
                log_write_error_information("Failed to close fd " + to_string(fd_) + ": " + strerror(errno));
    }

    FdGuard(FdGuard &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ != -1)
                if (close(fd_) == -1)
                    log_write_error_information("Failed to close fd " + to_string(fd_) + " in move assignment: " + strerror(errno));
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
            if (close(fd_) == -1)
                log_write_error_information("Failed to close fd " + to_string(fd_) + " in reset: " + strerror(errno));
        fd_ = fd;
    }

    int release()
    {
        int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }
};

string compile_files(const vector<string> &instructions)
{
    if (instructions.empty())
    {
        log_write_error_information("compile_files received empty instruction list.");
        return "Error: Empty instruction list provided.";
    }

    int stderr_pipe_fds[2];
    if (pipe2(stderr_pipe_fds, O_CLOEXEC) < 0)
    {
        if (errno == ENOSYS)
        {
            if (pipe(stderr_pipe_fds) < 0)
            {
                string error_msg = "Failed to create pipe: " + string(strerror(errno));
                log_write_error_information(error_msg);
                return "Error: " + error_msg;
            }
            if (fcntl(stderr_pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 ||
                fcntl(stderr_pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
            {
                string error_msg = "Failed to set FD_CLOEXEC on pipe: " + string(strerror(errno));
                log_write_error_information(error_msg);
                close(stderr_pipe_fds[0]);
                close(stderr_pipe_fds[1]);
                return "Error: " + error_msg;
            }
        }
        else
        {
            string error_msg = "Failed to create pipe with pipe2: " + string(strerror(errno));
            log_write_error_information(error_msg);
            return "Error: " + error_msg;
        }
    }

    FdGuard pipe_read_end(stderr_pipe_fds[0]);
    FdGuard pipe_write_end(stderr_pipe_fds[1]);

    pid_t pid = fork();

    if (pid == 0)
    {
        pipe_read_end.reset();

        if (dup2(pipe_write_end.get(), STDERR_FILENO) < 0)
            _exit(EXIT_FAILURE);
        pipe_write_end.reset();

        vector<const char *> argv;
        argv.push_back("g++");
        for (const string &arg : instructions)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        execvp(argv[0], const_cast<char *const *>(argv.data()));
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
        {
            // replace(error_output.begin(), error_output.end(), '\n', ' ');
            log_write_error_information("Compiler stderr output captured");
        }

        int child_status;
        if (waitpid(pid, &child_status, 0) == -1)
        {
            string error_msg = "waitpid failed for PID " + to_string(pid) + ": " + string(strerror(errno));
            log_write_error_information(error_msg);
            return "Error: " + error_msg + "\n" + error_output;
        }

        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
            {
                log_write_error_information("Compilation successful for PID " + to_string(pid));
                return error_output;
            }
            else
            {
                log_write_error_information("Compilation failed or child exec failed (PID " + to_string(pid) + ") with exit code: " + to_string(exit_code));
                return error_output;
            }
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            string signal_str = strsignal(term_signal) ? strsignal(term_signal) : "Unknown signal";
            log_write_error_information("Compiler process (PID " + to_string(pid) + ") terminated by signal: " + to_string(term_signal) + " (" + signal_str + ")");
            return error_output + "\nError: Process terminated by signal " + to_string(term_signal);
        }
        else
        {
            log_write_error_information("Compiler process (PID " + to_string(pid) + ") terminated abnormally.");
            return error_output + "\nError: Process terminated abnormally.";
        }
    }
    else
    {
        string error_msg = "Failed to fork process: " + string(strerror(errno));
        log_write_error_information(error_msg);
        return "Error: " + error_msg;
    }
}

tuple<bool, string, string> execute_executable(const vector<string> &command_line, const string &input_filename)
{
    string out_filename = "";
    string err_filename = "";

    if (command_line.empty())
    {
        log_write_error_information("execute_executable received empty command line: even no executable given");
        return {true, "execute_executable received empty command line: even no executable given", ""};
    }

    auto now = chrono::system_clock::now();
    auto now_c = chrono::system_clock::to_time_t(now);
    long timestamp = static_cast<long>(now_c);

    char out_filename_buf[FILENAME_BUFFER_SIZE];
    char err_filename_buf[FILENAME_BUFFER_SIZE];

    snprintf(out_filename_buf, FILENAME_BUFFER_SIZE, "%s/%ld.output", OUT_DIRECTORY, timestamp);
    snprintf(err_filename_buf, FILENAME_BUFFER_SIZE, "%s/%ld.err", OUT_DIRECTORY, timestamp);

    out_filename = out_filename_buf;
    err_filename = err_filename_buf;

    FdGuard input_fd_guard;
    FdGuard output_fd_guard;
    FdGuard error_fd_guard;

    if (!input_filename.empty())
    {
        int in_fd = open(input_filename.c_str(), O_RDONLY | O_CLOEXEC);
        if (in_fd < 0)
        {
            string error_info = "Failed to open input file '" + input_filename + "': " + strerror(errno);
            log_write_error_information(error_info);
            return {true, move(error_info), ""};
        }
        input_fd_guard.reset(in_fd);
    }

    int out_fd = open(out_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out_fd < 0)
    {
        string error_info = "Failed to open output file '" + out_filename + "': " + strerror(errno);
        log_write_error_information(error_info);
        return {true, move(error_info), ""};
    }
    output_fd_guard.reset(out_fd);

    int err_fd = open(err_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (err_fd < 0)
    {
        string error_info = "Failed to open error file '" + err_filename + "': " + strerror(errno);
        log_write_error_information(error_info);
        return {true, move(error_info), ""};
    }
    error_fd_guard.reset(err_fd);

    pid_t pid = fork();

    if (pid == 0)
    {
        if (input_fd_guard.get() != -1)
        {
            if (dup2(input_fd_guard.get(), STDIN_FILENO) < 0)
                _exit(EXIT_FAILURE);
            input_fd_guard.reset();
        }

        if (dup2(output_fd_guard.get(), STDOUT_FILENO) < 0)
            _exit(EXIT_FAILURE);
        output_fd_guard.reset();

        if (dup2(error_fd_guard.get(), STDERR_FILENO) < 0)
            _exit(EXIT_FAILURE);
        error_fd_guard.reset();

        vector<const char *> argv;
        for (const string &arg : command_line)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        execvp(command_line[0].c_str(), const_cast<char *const *>(argv.data()));
        _exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        int child_status;
        if (waitpid(pid, &child_status, 0) == -1)
        {
            string errno_information = string(strerror(errno));
            log_write_error_information("waitpid failed for PID " + to_string(pid) + ": " + errno_information);
            if (out_filename.length() or err_filename.length())
                return {true,
                        "waitpid failed for PID " + to_string(pid) + ": " + errno_information + "\nfile(s) created:" + out_filename + ',' + err_filename,
                        ""};
            else
                return {true, "waitpid failed for PID " + to_string(pid) + ": " + errno_information, ""};
        }

        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
                log_write_regular_information("Executable process (PID " + to_string(pid) + ") completed successfully.");
            else
                log_write_error_information("Executable process (PID " + to_string(pid) + ") failed with exit code: " + to_string(exit_code));
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            string signal_str = strsignal(term_signal) ? strsignal(term_signal) : "Unknown signal";
            log_write_error_information("Executable process (PID " + to_string(pid) + ") terminated by signal: " + to_string(term_signal) + " (" + signal_str + ")");
        }
        else
            log_write_error_information("Executable process (PID " + to_string(pid) + ") terminated abnormally.");

        return {false, out_filename, err_filename};
    }
    else
    {
        string info = "Failed to fork process: " + string(strerror(errno));
        log_write_error_information(info);
        return {true, move(info), ""};
    }
}
