#define _TACKLE_CLIENT_CPP
#include "cloud-compile-backend.hpp"

ThreadStatCode tackle_client(void *client_socket)
{
    Socket *client = static_cast<Socket *>(client_socket);
    stringstream stream = client->receive();
    string token;
    stream >> token;
    auto command_search_result = main_thread_process.find(token);
    if (command_search_result != main_thread_process.end())
        command_search_result->second(&stream);
    else
    {
        string error_info("Client sent unkown command: ");
        error_info += token;
        error_info += "\nConnection canceled.";
        client->send("Unkown Command.");
        log_write_error_information(error_info);
        delete client;
        return ThreadStatCode::CLIENT_COMMAND_ERROR;
    }
    delete client;
    log_write_regular_information("Process OK. Client closed.");
    return ThreadStatCode::SUCCESS;
}

ThreadStatCode receive_file(void *string_stream)
{
    stringstream *stream = static_cast<stringstream *>(string_stream);
    string file_name;
    *stream >> file_name;

    using namespace filesystem;
    char new_file[FILENAME_BUFFER_SIZE];
    sprintf(new_file, "src/%s", file_name.c_str());
    fstream output_file(new_file, ios::out | ios::trunc);
    output_file << stream->str();
    output_file.close();

    string log_info("New received file at: ");
    log_info += new_file;
    log_write_regular_information(log_info);
    return ThreadStatCode::SUCCESS;
}

ThreadStatCode receive_than_compile(void *string_stream)
{
    stringstream *stream = static_cast<stringstream *>(string_stream);
    string file_name;
    *stream >> file_name;

    using namespace filesystem;
    char new_file[FILENAME_BUFFER_SIZE];
    sprintf(new_file, "src/%s", file_name.c_str());
    fstream output_file(new_file, ios::out | ios::trunc);
    output_file << stream->str();
    output_file.close();

    string log_info("New received file at: ");
    log_info += new_file;
    log_write_regular_information(log_info);

    list<string> instructions;
    instructions.emplace_back(new_file);
    return compile_files(&instructions);
}
