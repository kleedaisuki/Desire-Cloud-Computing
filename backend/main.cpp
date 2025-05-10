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

#define _MAIN_CPP
#include "cloud-compile-backend.hpp"
using namespace net;

static string readFileContentToString(const filesystem::path &filePath)
{
    ifstream file_stream(filePath, ios::binary);
    if (!file_stream.is_open())
    {
        string error_msg = "Error: Could not open file for reading: " + filePath.string();
        log_write_error_information("readFileContentToString: " + error_msg);
        return error_msg;
    }

    file_stream.unsetf(ios::skipws);

    file_stream.seekg(0, ios::end);
    streampos file_size_pos = file_stream.tellg();
    file_stream.seekg(0, ios::beg);

    if (file_size_pos == static_cast<streampos>(-1) || file_size_pos < 0)
    {
        string error_msg = "Error: Could not determine size of file (or file is invalid): " + filePath.string();
        log_write_error_information("readFileContentToString: " + error_msg);
        return error_msg;
    }

    string content;
    if (file_size_pos > 0)
    {
        try
        {
            content.reserve(static_cast<string::size_type>(file_size_pos));
        }
        catch (const length_error &le)
        {
            string error_msg = "Error: File too large to reserve memory for reading: " + filePath.string() + " (" + le.what() + ")";
            log_write_error_information("readFileContentToString: " + error_msg);
            return error_msg;
        }
    }

    try
    {
        content.assign((istreambuf_iterator<char>(file_stream)),
                       istreambuf_iterator<char>());
    }
    catch (const exception &e)
    {
        string error_msg = "Error: Exception during assigning file content to string for file: " + filePath.string() + " (" + e.what() + ")";
        log_write_error_information("readFileContentToString: " + error_msg);
        return error_msg;
    }

    if (file_stream.bad())
    {
        string error_msg = "Error: Failed while reading file content (badbit set) from: " + filePath.string();
        log_write_error_information("readFileContentToString: " + error_msg);
        return error_msg;
    }

    return content;
}

int main(int argc, char *argv[])
{
    EventLoop loop;
    TcpServer server(&loop, DEFAULT_PORT, "k-SI");

    server.set_connection_callback([](const TcpConnectionPtr &conn)
                                   {
        if (conn->connected()) 
        {
            char peer_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &conn->peer_address().sin_addr, peer_ip, sizeof(peer_ip));
            uint16_t peer_port = ntohs(conn->peer_address().sin_port);
            log_write_regular_information("Client connected: " + conn->name() + " from " +
                                           string(peer_ip) + ":" + to_string(peer_port));
        } 
        else 
            log_write_regular_information("Client disconnected: " + conn->name()); });

    server.register_protocol_handler(
        "compile-execute",
        [&](const TcpConnectionPtr &conn, const string &incoming_tag, string_view payload) -> TcpServer::ProtocolHandlerPair
        {
            const string SRC_DIR_STR = "src";
            const string OUT_DIR_STR = OUT_DIRECTORY;

            size_t null_pos = payload.find('\0');
            if (null_pos == string_view::npos)
            {
                string err_msg_content = "Invalid payload: Missing null terminator.";
                log_write_error_information("compile-execute handler: " + err_msg_content);

                string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                if (!packaged_error_info.empty())
                {
                    conn->send(packaged_error_info);
                }
                else
                {
                    log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                }
                return {incoming_tag, string(payload)};
            }

            string original_filename_str(payload.substr(0, null_pos));
            string_view file_content_sv = payload.substr(null_pos + 1);

            if (original_filename_str.empty())
            {
                string err_msg_content = "Invalid payload: Original filename is empty.";
                log_write_error_information("compile-execute handler: " + err_msg_content);

                string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                if (!packaged_error_info.empty())
                {
                    conn->send(packaged_error_info);
                }
                else
                {
                    log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                }
                return {incoming_tag, string(payload)};
            }

            log_write_regular_information("compile-execute: Received request for file: " + original_filename_str + " with content length: " + to_string(file_content_sv.length()));

            try
            {

                filesystem::path original_fs_path(original_filename_str);
                string original_basename = original_fs_path.stem().string();
                string original_extension = original_fs_path.extension().string();

                auto now_timepoint = chrono::system_clock::now();
                auto now_epoch_ms = chrono::duration_cast<chrono::milliseconds>(now_timepoint.time_since_epoch()).count();
                string timestamp_str = to_string(now_epoch_ms);

                string new_source_filename_stem = original_basename + "-" + timestamp_str;
                string new_source_filename = new_source_filename_stem + original_extension;

                filesystem::path src_dir_path(SRC_DIR_STR);
                filesystem::path out_dir_path(OUT_DIR_STR);

                if (!filesystem::exists(src_dir_path))
                    filesystem::create_directories(src_dir_path);
                if (!filesystem::exists(out_dir_path))
                    filesystem::create_directories(out_dir_path);

                filesystem::path source_filepath = src_dir_path / new_source_filename;
                string output_executable_name = new_source_filename_stem + ".out";
                filesystem::path output_executable_path = out_dir_path / output_executable_name;

                {
                    ofstream src_file(source_filepath, ios::binary | ios::trunc);
                    if (!src_file.is_open())
                    {
                        string err_msg_content = "Failed to create/open source file for writing: " + source_filepath.string();
                        log_write_error_information("compile-execute handler: " + err_msg_content);
                        string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                        if (!packaged_error_info.empty())
                            conn->send(packaged_error_info);
                        else
                            log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                        return {incoming_tag, string(payload)};
                    }
                    src_file.write(file_content_sv.data(), file_content_sv.length());
                    if (src_file.fail())
                    {
                        string err_msg_content = "Failed to write content to source file: " + source_filepath.string();
                        log_write_error_information("compile-execute handler: " + err_msg_content);
                        src_file.close();
                        filesystem::remove(source_filepath);
                        string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                        if (!packaged_error_info.empty())
                            conn->send(packaged_error_info);
                        else
                            log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                        return {incoming_tag, string(payload)};
                    }
                    src_file.close();
                    log_write_regular_information("Source file saved successfully: " + source_filepath.string());
                }

                vector<string> compile_instructions = {
                    "g++", "-std=c++20", "-Wall", "-Wextra", "-pedantic",
                    source_filepath.string(),
                    "-o", output_executable_path.string()};
                log_write_regular_information("Compiling: g++ -std=c++20 " + source_filepath.string() + " -o " + output_executable_path.string());
                string compile_stderr_output = compile_files(compile_instructions);

                bool compilation_produced_executable = filesystem::exists(output_executable_path) &&
                                                       !filesystem::is_empty(output_executable_path);

                if (!compilation_produced_executable)
                {
                    string errinfo_filename = new_source_filename_stem + ".errinfo";
                    filesystem::path errinfo_filepath = out_dir_path / errinfo_filename;
                    {
                        ofstream err_info_file(errinfo_filepath, ios::binary | ios::trunc);
                        if (err_info_file.is_open())
                        {
                            err_info_file << compile_stderr_output;
                            err_info_file.close();
                            log_write_regular_information("Compilation error info saved to: " + errinfo_filepath.string());
                        }
                        else
                        {
                            log_write_error_information("Failed to write compilation error info to: " + errinfo_filepath.string() + ". Stderr was:\n" + compile_stderr_output);
                        }
                    }

                    string error_for_client = compile_stderr_output;
                    if (error_for_client.empty())
                    {
                        error_for_client = "Compilation failed to produce an executable, and no specific error message was captured from compiler stderr.";
                    }
                    log_write_error_information("compile-execute handler: Compilation failed for " + source_filepath.string() + ". Stderr/Info: " + error_for_client);

                    string packaged_error_info = TcpServer::package_message("error-information", error_for_client);
                    if (!packaged_error_info.empty())
                        conn->send(packaged_error_info);
                    else
                        log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                    return {incoming_tag, string(payload)};
                }

                if (!compile_stderr_output.empty())
                {
                    log_write_warning_information("Compilation for " + source_filepath.string() + " succeeded but produced stderr (e.g., warnings):\n" + compile_stderr_output);
                }
                log_write_regular_information("Compilation successful for " + source_filepath.string() + ". Executable: " + output_executable_path.string());

                vector<string> exec_command = {output_executable_path.string()};
                log_write_regular_information("Executing: " + output_executable_path.string());
                auto [exec_has_error, result_file1_str, result_file2_str] = execute_executable(exec_command, "" /* no stdin file */);

                string exec_response_content_for_client;
                if (exec_has_error)
                {
                    exec_response_content_for_client = result_file1_str;
                    log_write_error_information("compile-execute handler: Execution failed for " + output_executable_path.string() + ". Error: " + exec_response_content_for_client);

                    string packaged_error_info = TcpServer::package_message("error-information", exec_response_content_for_client);
                    if (!packaged_error_info.empty())
                        conn->send(packaged_error_info);
                    else
                        log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                    return {incoming_tag, string(payload)};
                }
                else
                {
                    filesystem::path exec_output_filepath(result_file1_str);
                    filesystem::path exec_error_filepath(result_file2_str);

                    string output_content = readFileContentToString(exec_output_filepath);
                    string error_content = readFileContentToString(exec_error_filepath);

                    bool output_read_error = output_content.rfind("Error: Could not open file", 0) == 0 || output_content.rfind("Error: Failed while reading file", 0) == 0 || output_content.rfind("Error: Could not determine size", 0) == 0 || output_content.rfind("Error: File too large", 0) == 0 || output_content.rfind("Error: Exception during assigning", 0) == 0;
                    bool error_read_error = error_content.rfind("Error: Could not open file", 0) == 0 || error_content.rfind("Error: Failed while reading file", 0) == 0 || error_content.rfind("Error: Could not determine size", 0) == 0 || error_content.rfind("Error: File too large", 0) == 0 || error_content.rfind("Error: Exception during assigning", 0) == 0;

                    if (output_read_error)
                    {
                        log_write_error_information("Failed to read execution output file: " + exec_output_filepath.string() + ". Content/Error: " + output_content);
                    }
                    if (error_read_error)
                    {
                        log_write_error_information("Failed to read execution error file: " + exec_error_filepath.string() + ". Content/Error: " + error_content);
                    }
                    stringstream combined_content_ss;
                    combined_content_ss << "--- stdout ---\n";
                    combined_content_ss << (output_read_error ? ("Failed to read " + exec_output_filepath.string() + ". See server logs for details.\n") : output_content);
                    combined_content_ss << "\n--- stderr ---\n";
                    combined_content_ss << (error_read_error ? ("Failed to read " + exec_error_filepath.string() + ". See server logs for details.\n") : error_content);
                    exec_response_content_for_client = combined_content_ss.str();

                    log_write_regular_information("Execution of " + output_executable_path.string() + " completed. Output/Err captured.");
                }

                return {incoming_tag, original_filename_str + '\0' + exec_response_content_for_client};
            }
            catch (const filesystem::filesystem_error &e)
            {
                string err_msg_content = "Filesystem error in compile-execute handler: " + string(e.what());
                log_write_error_information("compile-execute handler: " + err_msg_content);
                string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                if (!packaged_error_info.empty())
                    conn->send(packaged_error_info);
                else
                    log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                return {incoming_tag, string(payload)};
            }
            catch (const exception &e)
            {
                string err_msg_content = "Standard exception in compile-execute handler: " + string(e.what());
                log_write_error_information("compile-execute handler: " + err_msg_content);
                string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                if (!packaged_error_info.empty())
                    conn->send(packaged_error_info);
                else
                    log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                return {incoming_tag, string(payload)};
            }
            catch (...)
            {
                string err_msg_content = "Unknown error occurred in compile-execute handler.";
                log_write_error_information("compile-execute handler: " + err_msg_content);
                string packaged_error_info = TcpServer::package_message("error-information", err_msg_content);
                if (!packaged_error_info.empty())
                    conn->send(packaged_error_info);
                else
                    log_write_error_information("compile-execute handler: Failed to package 'error-information' for client " + conn->name());
                return {incoming_tag, string(payload)};
            }
        });

    server.register_protocol_handler(
        "Hello",
        [](const TcpConnectionPtr &conn, const string &tag, string_view payload) -> TcpServer::ProtocolHandlerPair
        {
            log_write_regular_information("Hello protocol handled for " + conn->name());
            return {"Hello", "Hello. Communication link established with server."};
        });

    server.start();
    loop.loop();
}

struct global
{
    global(void)
    {
        using namespace filesystem;

        try
        {
            make_sure_log_file();
            log_write_regular_information("Program Starts. Directories checked/created. Logger initialized.");
        }
        catch (const runtime_error &e)
        {
            cerr << "Runtime error during logger initialization: " << e.what() << endl;
            throw;
        }
        catch (...)
        {
            cerr << "Unknown error during logger initialization." << endl;
        }

        try
        {
            if (!exists(LOG_DIRECTORY))
                create_directory(LOG_DIRECTORY);
            if (!exists("bin"))
                create_directory("bin");
            if (!exists("src"))
                create_directory("src");
            if (!exists(OUT_DIRECTORY))
                create_directory(OUT_DIRECTORY);
        }
        catch (const filesystem::filesystem_error &e)
        {
            log_write_error_information("Filesystem error during directory creation: " + string(e.what()));
        }
    }

    ~global(void)
    {
        log_write_regular_information("Program Exiting. Closing log file.");
        try
        {
            close_log_file();
        }
        catch (const runtime_error &e)
        {
            cerr << "Runtime error during logger shutdown: " << e.what() << endl;
        }
        catch (...)
        {
            cerr << "Unknown error during logger shutdown." << endl;
        }
    }
} automatic;
