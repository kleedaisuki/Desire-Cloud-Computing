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

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream> // Klee酱注意：需要包含 fstream 用于文件操作
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>      // Klee酱注意：需要包含 cstring 用于 strerror 和 strsignal
#include <memory>       // Klee酱注意：需要包含 memory 用于 unique_ptr
#include <system_error> // Klee酱注意：需要包含 system_error 用于错误处理
#include <chrono>       // Klee酱注意：需要包含 chrono 用于时间戳
#include <filesystem>   // Klee酱注意：需要包含 filesystem 来创建目录 (C++17)
#include <utility>      // Klee酱注意：需要包含 utility 用于 std::pair 和 std::move
#include <optional>     // Klee酱注意：可以考虑使用 optional，但这里我们先用 string 返回错误信息

/**
 * @brief FdGuard 类 (RAII for file descriptors)
 * Klee酱注意：这个类用于自动管理文件描述符，确保它们在不再需要时被关闭，防止资源泄漏。
 * 它支持移动语义，但禁止复制，符合资源管理的最佳实践。
 */
class FdGuard
{
    int fd_ = -1;

public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}
    ~FdGuard()
    {
        if (fd_ != -1)
        {
            // Klee酱注意：在析构函数中关闭文件描述符
            if (close(fd_) == -1)
            {
                // Klee酱注意：析构函数中最好不要抛出异常，这里记录一个错误
                log_write_error_information("Failed to close fd " + std::to_string(fd_) + ": " + strerror(errno));
            }
        }
    }

    // Klee酱注意：移动构造函数，转移资源所有权
    FdGuard(FdGuard &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    // Klee酱注意：移动赋值运算符，转移资源所有权并释放旧资源
    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ != -1)
            {
                if (close(fd_) == -1)
                {
                    log_write_error_information("Failed to close fd " + std::to_string(fd_) + " in move assignment: " + strerror(errno));
                }
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // Klee酱注意：禁止拷贝构造和拷贝赋值，防止资源被多次管理或释放
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;

    // Klee酱注意：获取原始文件描述符
    int get() const { return fd_; }

    // Klee酱注意：重置 FdGuard，管理一个新的文件描述符，并关闭旧的（如果有）
    void reset(int fd = -1)
    {
        if (fd_ != -1)
        {
            if (close(fd_) == -1)
            {
                log_write_error_information("Failed to close fd " + std::to_string(fd_) + " in reset: " + strerror(errno));
            }
        }
        fd_ = fd;
    }

    // Klee酱注意：释放对文件描述符的管理权，返回原始 fd，并将其置为 -1
    // 调用者需要负责关闭返回的文件描述符
    int release()
    {
        int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }
};

/**
 * @brief 编译给定的源文件。
 * @param instructions 包含编译参数（通常是文件名）的向量。
 * @return std::string 返回编译过程中的标准错误输出。如果发生严重错误（如 pipe, fork 失败），则返回描述错误的字符串。
 * Klee酱注意：这个函数现在返回 string 而不是 ThreadStatCode。它会尝试捕获编译器的 stderr 输出。
 */
std::string compile_files(const std::vector<std::string> &instructions)
{
    if (instructions.empty())
    {
        log_write_error_information("compile_files received empty instruction list.");
        return "Error: Empty instruction list provided."; // Klee酱注意：返回错误信息字符串
    }

    int stderr_pipe_fds[2];
    // Klee酱注意：优先使用 pipe2 创建管道并设置 O_CLOEXEC 原子地防止文件描述符泄漏到 exec 后的进程
    if (pipe2(stderr_pipe_fds, O_CLOEXEC) < 0)
    {
        // Klee酱注意：如果 pipe2 不支持 (ENOSYS)，则回退到 pipe + fcntl
        if (errno == ENOSYS)
        {
            if (pipe(stderr_pipe_fds) < 0)
            {
                std::string error_msg = "Failed to create pipe: " + std::string(strerror(errno));
                log_write_error_information(error_msg);
                return "Error: " + error_msg; // Klee酱注意：返回错误信息字符串
            }
            // Klee酱注意：手动为 pipe 创建的文件描述符设置 close-on-exec 标志
            if (fcntl(stderr_pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 ||
                fcntl(stderr_pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
            {
                std::string error_msg = "Failed to set FD_CLOEXEC on pipe: " + std::string(strerror(errno));
                log_write_error_information(error_msg);
                close(stderr_pipe_fds[0]); // Klee酱注意：设置失败时需要手动关闭 fd
                close(stderr_pipe_fds[1]);
                return "Error: " + error_msg; // Klee酱注意：返回错误信息字符串
            }
        }
        else
        {
            std::string error_msg = "Failed to create pipe with pipe2: " + std::string(strerror(errno));
            log_write_error_information(error_msg);
            return "Error: " + error_msg; // Klee酱注意：返回错误信息字符串
        }
    }

    // Klee酱注意：使用 FdGuard 管理管道的两端，确保它们会被自动关闭
    FdGuard pipe_read_end(stderr_pipe_fds[0]);
    FdGuard pipe_write_end(stderr_pipe_fds[1]);

    pid_t pid = fork();

    if (pid == 0) // Klee酱注意：子进程代码块
    {
        // Klee酱注意：子进程不需要读取管道，关闭读取端
        pipe_read_end.reset(); // FdGuard 会负责 close

        // Klee酱注意：将标准错误重定向到管道的写入端
        if (dup2(pipe_write_end.get(), STDERR_FILENO) < 0)
        {
            // Klee酱注意：重定向失败，直接通过管道（如果还能用）或原始 stderr 报告错误并退出
            // 这里很难向父进程报告具体的 dup2 错误，父进程会通过子进程的非零退出码得知失败
            std::cerr << "Child: Failed to redirect stderr: " << strerror(errno) << std::endl;
            _exit(EXIT_FAILURE); // Klee酱注意：使用 _exit 避免调用 atexit handlers 和刷新 stdio 缓冲区
        }
        // Klee酱注意：关闭原始的管道写入端 fd，因为它已经被复制到 STDERR_FILENO
        // pipe_write_end 的 FdGuard 在子进程退出时会自动处理，或者在这里显式 reset 也可以
        // pipe_write_end.reset(); // 显式关闭也是好习惯

        std::vector<const char *> argv;
        std::string command = "g++"; // Klee酱注意：硬编码编译器为 g++
        argv.push_back(command.c_str());
        for (const std::string &arg : instructions)
        {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr); // Klee酱注意：argv 数组必须以 nullptr 结尾

        // Klee酱注意：执行编译器命令
        execvp(argv[0], const_cast<char *const *>(argv.data()));

        // Klee酱注意：如果 execvp 返回，说明执行失败
        std::cerr << "Child: Failed to execute " << argv[0] << ": " << strerror(errno) << std::endl;
        _exit(EXIT_FAILURE); // Klee酱注意：报告 execvp 失败
    }
    else if (pid > 0) // Klee酱注意：父进程代码块
    {
        // Klee酱注意：父进程不需要写入管道，关闭写入端
        pipe_write_end.reset(); // FdGuard 会负责 close

        std::stringstream error_output_stream;
        char read_buffer[ERROR_BUFFER_SIZE];
        ssize_t bytes_read;

        // Klee酱注意：从管道的读取端读取子进程的 stderr 输出
        while ((bytes_read = read(pipe_read_end.get(), read_buffer, sizeof(read_buffer) - 1)) > 0)
        {
            read_buffer[bytes_read] = '\0'; // Klee酱注意：确保缓冲区以 null 结尾
            error_output_stream << read_buffer;
        }

        // Klee酱注意：检查读取过程中是否发生错误
        if (bytes_read < 0)
        {
            log_write_error_information("Error reading from pipe: " + std::string(strerror(errno)));
            // Klee酱注意：即使读取出错，仍然尝试处理已读取的内容和子进程状态
        }

        std::string error_output = error_output_stream.str();
        // Klee酱注意：如果捕获到了 stderr 输出，就记录下来
        if (!error_output.empty())
        {
            // Klee酱注意：移除日志中的换行符，根据需求调整
            // std::replace(error_output.begin(), error_output.end(), '\n', ' ');
            log_write_error_information("Compiler stderr output captured"); // 只记录捕获到了输出
        }

        int child_status;
        // Klee酱注意：等待子进程结束
        if (waitpid(pid, &child_status, 0) == -1)
        {
            std::string error_msg = "waitpid failed for PID " + std::to_string(pid) + ": " + std::string(strerror(errno));
            log_write_error_information(error_msg);
            // Klee酱注意：waitpid 失败，之前的 stderr 可能不完整或没有，返回错误信息
            return "Error: " + error_msg + "\n" + error_output;
        }

        // Klee酱注意：检查子进程的退出状态
        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
            {
                log_write_error_information("Compilation successful for PID " + std::to_string(pid));
                // Klee酱注意：编译成功，返回捕获的 stderr (可能为空)
                return error_output;
            }
            else
            {
                log_write_error_information("Compilation failed or child exec failed (PID " + std::to_string(pid) + ") with exit code: " + std::to_string(exit_code));
                // Klee酱注意：编译失败或子进程启动失败，返回捕获的 stderr (可能包含错误信息)
                return error_output;
            }
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            std::string signal_str = strsignal(term_signal) ? strsignal(term_signal) : "Unknown signal";
            log_write_error_information("Compiler process (PID " + std::to_string(pid) + ") terminated by signal: " + std::to_string(term_signal) + " (" + signal_str + ")");
            // Klee酱注意：子进程被信号终止，返回捕获的 stderr 和信号信息
            return error_output + "\nError: Process terminated by signal " + std::to_string(term_signal);
        }
        else
        {
            log_write_error_information("Compiler process (PID " + std::to_string(pid) + ") terminated abnormally.");
            // Klee酱注意：未知终止原因，返回捕获的 stderr 和通用错误信息
            return error_output + "\nError: Process terminated abnormally.";
        }
    }
    else // Klee酱注意：fork 失败
    {
        std::string error_msg = "Failed to fork process: " + std::string(strerror(errno));
        log_write_error_information(error_msg);
        // Klee酱注意：需要在 fork 失败时关闭之前创建的管道 fd
        // FdGuard 会在作用域结束时自动处理 stderr_pipe_fds[0] 和 stderr_pipe_fds[1] (如果它们被赋值了)
        // 但在这里，它们可能还没被 FdGuard 管理，或者 FdGuard 对象还没创建
        // 确保在 pipe 成功但 fork 失败时关闭 fd
        // 然而，FdGuard 在 fork 失败前已经创建并接管了 fd，所以它们会被自动关闭
        return "Error: " + error_msg; // Klee酱注意：返回错误信息字符串
    }
}

/**
 * @brief 从输入流解析编译指令。
 * @param stream_owner 包含文件名的输入流。
 * @return bool 返回 true 如果指令解析和编译启动（fork）成功，否则返回 false。
 * Klee酱注意：这个函数现在返回 bool。编译的最终结果需要检查日志或 compile_files 的返回值。
 */
bool start_compile(std::stringstream stream_owner) // Klee酱注意：按值传递 stringstream 来获取所有权
{
    std::stringstream &stream = stream_owner; // Klee酱注意：使用引用方便操作

    auto instructions = std::make_unique<std::vector<std::string>>();
    std::string filename;
    const std::string source_directory = "src/"; // Klee酱注意：源文件目录

    while (stream >> filename)
    {
        if (!filename.empty())
        {
            try
            {
                instructions->push_back(source_directory + filename);
            }
            catch (const std::bad_alloc &e)
            {
                log_write_error_information("Memory allocation failed while building instructions: " + std::string(e.what()));
                return false; // Klee酱注意：内存分配失败，返回 false
            }
        }
    }

    // Klee酱注意：检查流读取过程中是否发生错误 (除了 EOF)
    if (stream.bad())
    {
        log_write_error_information("Error reading from input stringstream.");
        return false; // Klee酱注意：流读取错误，返回 false
    }

    if (instructions->empty())
    {
        log_write_error_information("No valid filenames found in the input stream. Nothing to compile.");
        return true; // Klee酱注意：没有文件需要编译，认为操作是“成功”的（没有失败）
    }

    // Klee酱注意：调用新的 compile_files 函数
    std::string compile_stderr = compile_files(*instructions); // Klee酱注意：传递 vector 的引用或值都可以

    // Klee酱注意：检查 compile_files 是否返回了错误信息或编译器 stderr 输出
    if (!compile_stderr.empty())
    {
        // Klee酱注意：判断返回的是否是表示严重错误的字符串
        if (compile_stderr.rfind("Error: ", 0) == 0)
        {
            log_write_error_information("Compile initiation failed: " + compile_stderr);
            return false; // Klee酱注意：编译启动失败（如 pipe/fork 失败）
        }
        else
        {
            // Klee酱注意：记录编译器实际的 stderr 输出
            log_write_error_information("Compiler stderr output:\n" + compile_stderr);
            // Klee酱注意：即使编译有警告或错误 (stderr 非空)，启动过程本身是成功的
            return true;
        }
    }
    else
    {
        // Klee酱注意：编译成功且 stderr 为空
        log_write_error_information("Compilation process started successfully and produced no stderr output.");
        return true; // Klee酱注意：编译启动成功
    }
}

/**
 * @brief 创建输出目录（如果不存在）。
 * Klee酱注意：这是一个辅助函数，确保日志和输出文件可以被创建。
 */
bool ensure_output_directory_exists()
{
    std::error_code ec;
    if (!std::filesystem::exists(OUT_DIRECTORY))
    {
        if (!std::filesystem::create_directory(OUT_DIRECTORY, ec))
        {
            log_write_error_information("Failed to create output directory '" + std::string(OUT_DIRECTORY) + "': " + ec.message());
            return false;
        }
        log_write_error_information("Created output directory '" + std::string(OUT_DIRECTORY) + "'");
    }
    return true;
}

/**
 * @brief 执行可执行文件，并重定向其 stdin, stdout, stderr。
 * @param command_line 包含可执行文件路径和参数的向量。
 * @param input_filename 用于重定向到子进程标准输入的文件名。如果为空，则不重定向 stdin。
 * @return std::pair<std::string, std::string> 返回包含重定向后的 stdout 和 stderr 文件名的 pair。
 * 如果发生严重错误（如 fork 失败），则 pair 的两个 string 都可能为空或包含错误信息。
 * Klee酱注意：这个函数重构变化最大！它现在接受输入文件名，并将 stdout/stderr 重定向到带时间戳的文件。
 * Klee酱注意：返回的是文件名，而不是 fstream 对象，这样更安全。
 */
std::pair<std::string, std::string> execute_executable(
    const std::vector<std::string> &command_line,
    const std::string &input_filename)
{
    std::string out_filename = "";
    std::string err_filename = "";

    if (command_line.empty())
    {
        log_write_error_information("execute_executable received empty command line.");
        return {out_filename, err_filename}; // Klee酱注意：返回空文件名表示失败
    }

    // Klee酱注意：确保输出目录存在
    if (!ensure_output_directory_exists())
    {
        return {out_filename, err_filename}; // Klee酱注意：无法创建目录，返回空文件名
    }

    // Klee酱注意：生成带时间戳的文件名
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    long timestamp = static_cast<long>(now_c); // Klee酱注意：使用 long 类型的时间戳

    char out_filename_buf[FILENAME_BUFFER_SIZE];
    char err_filename_buf[FILENAME_BUFFER_SIZE];

    snprintf(out_filename_buf, FILENAME_BUFFER_SIZE, "%s/%ld.output", OUT_DIRECTORY, timestamp);
    snprintf(err_filename_buf, FILENAME_BUFFER_SIZE, "%s/%ld.err", OUT_DIRECTORY, timestamp);

    out_filename = out_filename_buf;
    err_filename = err_filename_buf;

    // Klee酱注意：使用 FdGuard 来管理我们将要打开的文件描述符
    FdGuard input_fd_guard;
    FdGuard output_fd_guard;
    FdGuard error_fd_guard;

    // Klee酱注意：尝试打开输入文件（如果提供了）
    if (!input_filename.empty())
    {
        int in_fd = open(input_filename.c_str(), O_RDONLY | O_CLOEXEC);
        if (in_fd < 0)
        {
            log_write_error_information("Failed to open input file '" + input_filename + "': " + strerror(errno));
            return {"", ""}; // Klee酱注意：打开输入文件失败
        }
        input_fd_guard.reset(in_fd); // Klee酱注意：让 FdGuard 管理输入文件描述符
    }

    // Klee酱注意：创建并打开输出文件
    // O_CREAT: 如果文件不存在则创建
    // O_WRONLY: 只写模式打开
    // O_TRUNC: 如果文件存在则清空内容
    // O_CLOEXEC: 在 exec 后自动关闭此文件描述符（在子进程中我们希望它保持打开状态，但在父进程中希望它关闭）
    // 权限 0644: user(rw-), group(r--), other(r--)
    int out_fd = open(out_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out_fd < 0)
    {
        log_write_error_information("Failed to open output file '" + out_filename + "': " + strerror(errno));
        return {"", ""}; // Klee酱注意：打开输出文件失败
    }
    output_fd_guard.reset(out_fd); // Klee酱注意：让 FdGuard 管理输出文件描述符

    // Klee酱注意：创建并打开错误文件
    int err_fd = open(err_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (err_fd < 0)
    {
        log_write_error_information("Failed to open error file '" + err_filename + "': " + strerror(errno));
        // Klee酱注意：此时输出文件已打开，FdGuard 会负责关闭它
        return {"", ""}; // Klee酱注意：打开错误文件失败
    }
    error_fd_guard.reset(err_fd); // Klee酱注意：让 FdGuard 管理错误文件描述符

    pid_t pid = fork();

    if (pid == 0) // Klee酱注意：子进程代码块
    {
        // Klee酱注意：在子进程中，我们需要取消 CLOEXEC 标志，以便 dup2 后的描述符在 exec 后保持打开状态
        // 但是 dup2 会自动清除 CLOEXEC 标志，所以我们不需要手动 fcntl 清除
        // 参考: https://man7.org/linux/man-pages/man2/dup.2.html (FD_CLOEXEC section)

        // Klee酱注意：重定向标准输入（如果需要）
        if (input_fd_guard.get() != -1)
        {
            if (dup2(input_fd_guard.get(), STDIN_FILENO) < 0)
            {
                std::cerr << "Child: Failed to redirect stdin: " << strerror(errno) << std::endl;
                _exit(EXIT_FAILURE); // Klee酱注意：报告重定向失败
            }
            // Klee酱注意：关闭原始输入文件描述符，因为它已被复制到 STDIN_FILENO
            // FdGuard 会在子进程退出时自动关闭，但显式关闭更好
            input_fd_guard.reset();
        }

        // Klee酱注意：重定向标准输出
        if (dup2(output_fd_guard.get(), STDOUT_FILENO) < 0)
        {
            std::cerr << "Child: Failed to redirect stdout: " << strerror(errno) << std::endl;
            _exit(EXIT_FAILURE); // Klee酱注意：报告重定向失败
        }
        output_fd_guard.reset(); // Klee酱注意：关闭原始输出文件描述符

        // Klee酱注意：重定向标准错误
        if (dup2(error_fd_guard.get(), STDERR_FILENO) < 0)
        {
            std::cerr << "Child: Failed to redirect stderr: " << strerror(errno) << std::endl;
            _exit(EXIT_FAILURE); // Klee酱注意：报告重定向失败
        }
        error_fd_guard.reset(); // Klee酱注意：关闭原始错误文件描述符

        // Klee酱注意：准备 execvp 的参数列表
        std::vector<const char *> argv;
        for (const std::string &arg : command_line)
        {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr); // Klee酱注意：argv 必须以 nullptr 结尾

        // Klee酱注意：执行目标程序
        execvp(command_line[0].c_str(), const_cast<char *const *>(argv.data()));

        // Klee酱注意：如果 execvp 返回，说明执行失败
        std::cerr << "Child: Failed to execute '" << command_line[0] << "': " << strerror(errno) << std::endl;
        _exit(EXIT_FAILURE); // Klee酱注意：报告 execvp 失败
    }
    else if (pid > 0) // Klee酱注意：父进程代码块
    {
        // Klee酱注意：父进程不需要这些文件描述符了，FdGuard 会在离开作用域时自动关闭它们
        // input_fd_guard.reset(); // FdGuard 会自动处理
        // output_fd_guard.reset();
        // error_fd_guard.reset();
        // Klee酱注意：不再需要读取管道，移除相关代码和线程

        int child_status;
        // Klee酱注意：等待子进程结束
        if (waitpid(pid, &child_status, 0) == -1)
        {
            log_write_error_information("waitpid failed for PID " + std::to_string(pid) + ": " + std::string(strerror(errno)));
            // Klee酱注意：waitpid 失败，但文件可能已创建，返回文件名让调用者处理
            return {out_filename, err_filename};
        }

        // Klee酱注意：检查子进程退出状态并记录日志
        if (WIFEXITED(child_status))
        {
            int exit_code = WEXITSTATUS(child_status);
            if (exit_code == 0)
            {
                log_write_error_information("Executable process (PID " + std::to_string(pid) + ") completed successfully.");
            }
            else
            {
                // Klee酱注意：子进程非零退出，可能是程序本身错误，也可能是启动阶段错误（如重定向、execvp）
                // 具体的错误原因通常会在 stderr 文件中
                log_write_error_information("Executable process (PID " + std::to_string(pid) + ") failed with exit code: " + std::to_string(exit_code));
            }
        }
        else if (WIFSIGNALED(child_status))
        {
            int term_signal = WTERMSIG(child_status);
            std::string signal_str = strsignal(term_signal) ? strsignal(term_signal) : "Unknown signal";
            log_write_error_information("Executable process (PID " + std::to_string(pid) + ") terminated by signal: " + std::to_string(term_signal) + " (" + signal_str + ")");
        }
        else
        {
            log_write_error_information("Executable process (PID " + std::to_string(pid) + ") terminated abnormally.");
        }

        // Klee酱注意：无论子进程结果如何，都返回创建的输出和错误文件名
        return {out_filename, err_filename};
    }
    else // Klee酱注意：fork 失败
    {
        log_write_error_information("Failed to fork process: " + std::string(strerror(errno)));
        // Klee酱注意：FdGuard 会自动关闭已打开的文件描述符
        return {"", ""}; // Klee酱注意：返回空文件名表示 fork 失败
    }
}

// Klee酱注意：这里可以添加一些示例用法或测试代码
int main()
{
    // --- 示例：编译 ---
    std::stringstream compile_stream;
    compile_stream << " main.cpp utils.cpp ";                        // Klee酱注意：模拟输入流
    bool compile_started = start_compile(std::move(compile_stream)); // Klee酱注意：使用 std::move 转移流所有权

    if (compile_started)
    {
        log_write_error_information("Compilation process initiated.");
        // Klee酱注意：实际的编译结果需要检查日志中记录的 stderr 输出
    }
    else
    {
        log_write_error_information("Failed to initiate compilation process.");
        return 1; // Klee酱注意：启动失败，可以提前退出
    }

    // Klee酱注意：假设编译成功，生成了可执行文件 a.out (g++ 默认)

    // --- 示例：执行 ---
    std::vector<std::string> exec_args = {"./a.out", "arg1", "arg2"}; // Klee酱注意：可执行文件及其参数
    std::string input_file = "input.txt";                             // Klee酱注意：指定输入重定向文件

    // Klee酱注意：确保 input.txt 存在且有内容，否则子进程可能挂起或出错
    {
        std::ofstream infile(input_file);
        if (infile)
        {
            infile << "Hello from input file!\n";
        }
        else
        {
            log_write_error_information("Failed to create sample input file.");
            // return 1; // Klee酱注意：如果输入文件是必须的，这里应该处理错误
        }
    }

    log_write_error_information("Attempting to execute: ./a.out arg1 arg2 with input from input.txt");
    std::pair<std::string, std::string> output_files = execute_executable(exec_args, input_file);

    if (!output_files.first.empty() && !output_files.second.empty())
    {
        log_write_error_information("Execution finished. Output file: " + output_files.first + ", Error file: " + output_files.second);
        // Klee酱注意：调用者现在可以根据需要读取这两个文件的内容
        // 例如：
        // std::ifstream outfile(output_files.first, std::ios::binary);
        // std::ifstream errfile(output_files.second, std::ios::binary);
        // ... 读取文件内容 ...
    }
    else
    {
        log_write_error_information("Failed to execute the command or critical error occurred.");
        return 1;
    }

    return 0;
}