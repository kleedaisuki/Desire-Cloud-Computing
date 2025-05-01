#define _MAIN_CPP
#include "cloud-compile-backend.hpp"
using namespace net;

int main(int argc, char *argv[])
{
    initialize();
    if (argc != 2)
    {
        log_write_error_information("Usage: " + string(argv[0]) + " <port>");
        return EXIT_FAILURE;
    }
    uint16_t port = 0;
    try
    {
        port = static_cast<uint16_t>(stoi(argv[1]));
        if (port == 0)
            throw invalid_argument("Port cannot be 0");
    }
    catch (const exception &e)
    {
        log_write_error_information("Invalid port number '" + string(argv[1]) + "': " + e.what());
        return EXIT_FAILURE;
    }

    log_write_regular_information("Starting server on port " + to_string(port) + "...");
    log_write_regular_information("Global ThreadPool instance access confirmed.");

    EventLoop loop;
    TcpServer server(&loop, port);

    // Register ECHO handler
    server.register_handler("ECHO", [](const TcpConnectionPtr &conn, Buffer *buf) -> string
                            {
        log_write_regular_information("Handling ECHO for " + conn->name() + ", received " + to_string(buf->readable_bytes()) + " bytes.");
        return buf->retrieve_all_as_string(); });

    // Register TIME handler
    server.register_handler("TIME", [](const TcpConnectionPtr &conn, Buffer *buf) -> string
                            {
        log_write_regular_information("Handling TIME for " + conn->name());
        buf->retrieve_all();
        char time_buf[64];
        time_t t = time(nullptr);
        strftime(time_buf, sizeof(time_buf), "%F %T\r\n", localtime(&t));
        return string{time_buf}; });

    server.set_default_handler([](const TcpConnectionPtr &conn, Buffer *buf) -> string
                               {
        log_write_warning_information("Handling Unknown tag for " + conn->name());
        buf->retrieve_all();
        return string{"Unknown tag\r\n"}; });

    // Set connection callback (optional)
    server.set_connection_callback([](const TcpConnectionPtr &conn)
                                   {
        if (conn->connected()) {
             // Convert peer address to string for logging
             char peer_ip[INET_ADDRSTRLEN];
             ::inet_ntop(AF_INET, &conn->peer_address().sin_addr, peer_ip, sizeof(peer_ip));
             uint16_t peer_port = ntohs(conn->peer_address().sin_port);
             log_write_regular_information("Client connected: " + conn->name() + " from " +
                                           string(peer_ip) + ":" + to_string(peer_port));
        } else {
             log_write_regular_information("Client disconnected: " + conn->name());
        } });

    server.start();
    loop.loop();

    log_write_regular_information("Server exiting.");

    finalize();
}
