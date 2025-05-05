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

#define _CLASS_TCPSERVER_CPP
#include "network.hpp"
using namespace net;

TcpServer::TcpServer(EventLoop *loop, uint16_t port, string name, bool reuse_port)
    : loop_{loop},
      name_{move(name)},
      acceptor_{make_unique<Acceptor>(loop, port, reuse_port)},
      started_{false},
      next_conn_id_{1},
      connection_cb_{[](const TcpConnectionPtr &) { /* Default no-op */ }},
      write_complete_cb_{[](const TcpConnectionPtr &) { /* Default no-op */ }},
      default_protocol_handler_{[](const TcpConnectionPtr &conn, const string &tag, string_view /*payload*/) -> ProtocolHandlerPair
                                {
                                    log_write_warning_information("Using default protocol handler for unknown tag: " + tag + " on connection " + conn->name());
                                    return ProtocolHandlerPair("ERROR", "Error: Unknown protocol command '" + tag + "'");
                                }},
      default_handler_{[](const TcpConnectionPtr &conn, Buffer *buf) -> string
                       {
                           log_write_warning_information("Using legacy default handler for connection " + conn->name() + ". Buffer size: " + to_string(buf->readable_bytes()));
                           string received = buf->retrieve_all_as_string();
                           return "Error: Unrecognized command or data format: '" + received.substr(0, 50) + (received.length() > 50 ? "..." : "") + "'\r\n";
                       }}
{
    log_write_regular_information("Starting server on port " + to_string(port) + "...");
    acceptor_->set_new_connection_callback(
        [this](int sockfd, const sockaddr_in &peer_addr)
        {
            new_connection(sockfd, peer_addr);
        });
    log_write_regular_information("TcpServer created [" + name_ + "] on loop " + to_string(reinterpret_cast<uintptr_t>(loop_)));
}

TcpServer::~TcpServer()
{
    log_write_regular_information("TcpServer::~TcpServer [" + name_ + "] destructing");
    for (auto &[conn_name, conn] : connections_)
    {
        TcpConnectionPtr conn_copy(conn);
        conn.reset();
        if (conn_copy)
        {
            /* Default no-op */
        }
    }
    connections_.clear();
    log_write_regular_information("Server exited.");
}

void TcpServer::register_protocol_handler(const string &tag, ProtocolHandler cb)
{
    if (tag.length() > numeric_limits<uint8_t>::max())
    {
        log_write_error_information("Cannot register protocol handler: Tag length exceeds 255 bytes. Tag: " + tag);
        return;
    }
    protocol_handlers_.insert_or_assign(tag, cb);
    log_write_regular_information("Registered protocol handler for tag: " + tag);
}

void TcpServer::set_default_protocol_handler(ProtocolHandler cb)
{
    default_protocol_handler_ = move(cb);
    log_write_regular_information("Default protocol handler set.");
}

void TcpServer::register_handler(HandlerTag tag, Handler cb)
{
    if (tag.length() > numeric_limits<uint8_t>::max())
    {
        log_write_error_information("Cannot register legacy handler: Tag length exceeds 255 bytes. Tag: " + tag);
        return;
    }
    handlers_.insert_or_assign(tag, cb);
    log_write_regular_information("Registered legacy handler for tag: " + tag);
}

void TcpServer::set_default_handler(Handler cb)
{
    default_handler_ = move(cb);
    log_write_regular_information("Legacy default handler set.");
}

void TcpServer::set_connection_callback(const TcpConnection::ConnectionCallback &cb)
{
    connection_cb_ = cb;
}

void TcpServer::set_write_complete_callback(const TcpConnection::WriteCompleteCallback &cb)
{
    write_complete_cb_ = cb;
}

void TcpServer::start()
{
    if (!started_)
    {
        started_ = true;
        loop_->run_in_loop([this]()
                           {
             loop_->assert_in_loop_thread();
             acceptor_->listen();
             log_write_regular_information("TcpServer [" + name_ + "] started listening."); });
        log_write_regular_information("TcpServer [" + name_ + "] start requested."); // 模拟启动
    }
}

/* static */ string TcpServer::package_message(const string &tag, string_view payload)
{
    if (tag.length() > numeric_limits<uint8_t>::max())
    {
        log_write_error_information("package_message error: Tag length (" + to_string(tag.length()) + ") exceeds limit (255). Tag: " + tag);
        return "";
    }
    if (payload.length() > numeric_limits<uint32_t>::max())
    {
        log_write_error_information("package_message error: Payload length (" + to_string(payload.length()) + ") exceeds limit (UINT32_MAX).");
        return "";
    }

    uint8_t tag_len = static_cast<uint8_t>(tag.length());
    uint32_t payload_len_host = static_cast<uint32_t>(payload.length());
    uint32_t payload_len_net = htonl(payload_len_host);

    string message;
    size_t total_len = sizeof(tag_len) + tag.length() + sizeof(payload_len_net) + payload.length();
    message.reserve(total_len);

    message.push_back(static_cast<char>(tag_len));
    message.append(tag);
    message.append(reinterpret_cast<const char *>(&payload_len_net), sizeof(payload_len_net));
    message.append(payload.data(), payload.length());

    return message;
}

string TcpServer::on_message(const TcpConnectionPtr &conn, Buffer *buf)
{
    while (buf->readable_bytes() > 0)
    {
        size_t initial_readable = buf->readable_bytes();

        bool processed_by_protocol_attempt = attempt_protocol_processing(conn, buf);

        if (processed_by_protocol_attempt)
        {
            continue;
        }
        else
        {
            bool processed_by_legacy = process_legacy_fallback(conn, buf, initial_readable);
            break;
        }
    }
    return "";
}

bool TcpServer::attempt_protocol_processing(const TcpConnectionPtr &conn, Buffer *buf)
{
    uint8_t tag_len = 0;
    size_t header_len = 0;
    uint32_t payload_len = 0;
    size_t total_message_len = 0;
    string tag = "";
    size_t initial_readable = buf->readable_bytes();

    if (initial_readable < sizeof(uint8_t))
        return false;

    tag_len = static_cast<uint8_t>(*buf->peek());
    if (tag_len == 0 || tag_len >= 64)
        return false;

    header_len = sizeof(uint8_t) + tag_len + sizeof(uint32_t);
    if (initial_readable < header_len)
        return false;

    uint32_t payload_len_net;
    memcpy(&payload_len_net, buf->peek() + sizeof(uint8_t) + tag_len, sizeof(payload_len_net));
    payload_len = ntohl(payload_len_net);
    if (payload_len > kMaxPayloadSize)
    {
        log_write_error_information("TcpServer::attempt_protocol_processing [" + conn->name() + "] - Protocol Error: Payload length (" + to_string(payload_len) + ") exceeds limit. Closing connection.");
        conn->force_close();
        buf->retrieve_all();
        return true;
    }

    total_message_len = header_len + payload_len;
    if (initial_readable < total_message_len)
        return false;

    tag.assign(buf->peek() + sizeof(uint8_t), tag_len);

    auto it_proto = protocol_handlers_.find(tag);
    if (it_proto != protocol_handlers_.end())
    {
        execute_protocol_handler(it_proto->second, conn, buf, tag, header_len, payload_len);
        return true;
    }

    if (execute_legacy_handler_for_tag(tag, conn, buf))
    {
        if (buf->readable_bytes() < initial_readable)
            return true;
        else
        {
            log_write_warning_information("TcpServer::attempt_protocol_processing [" + conn->name() + "] - Legacy handler for tag '" + tag + "' was called but consumed no data.");
            return false;
        }
    }

    if (default_protocol_handler_)
    {
        execute_default_protocol_handler(default_protocol_handler_, conn, buf, tag, header_len, payload_len);
        return true;
    }

    log_write_warning_information("TcpServer::attempt_protocol_processing [" + conn->name() + "] - Valid protocol frame for tag '" + tag + "' but no handler found. Discarding frame.");
    buf->retrieve(total_message_len);
    return true;
}

void TcpServer::execute_protocol_handler(const ProtocolHandler &handler, const TcpConnectionPtr &conn, Buffer *buf, const string &tag, size_t header_len, uint32_t payload_len)
{
    ProtocolHandlerPair response;
    try
    {
        buf->retrieve(header_len);
        string_view payload_sv(buf->peek(), payload_len);
        size_t payload_to_consume = payload_len;
        response = handler(conn, tag, payload_sv);
        buf->retrieve(payload_to_consume);
    }
    catch (const exception &e)
    {
        log_write_error_information("ProtocolHandler exception for tag [" + tag + "] on connection [" + conn->name() + "]: " + string(e.what()));
        response.second = "Internal server error (protocol handler exception).";
        if (buf->readable_bytes() >= payload_len)
            buf->retrieve(payload_len);
    }
    catch (...)
    {
        log_write_error_information("Unknown ProtocolHandler exception for tag [" + tag + "] on connection [" + conn->name() + "].");
        response.second = "Unknown internal server error (protocol handler exception).";
        if (buf->readable_bytes() >= payload_len)
        {
            buf->retrieve(payload_len);
        }
    }

    if (!response.second.empty())
    {
        string packaged_response = TcpServer::package_message(response.first, response.second);
        if (!packaged_response.empty())
            conn->send(packaged_response);
    }
}

bool TcpServer::execute_legacy_handler_for_tag(const string &tag, const TcpConnectionPtr &conn, Buffer *buf)
{
    auto it_legacy = handlers_.find(tag);
    if (it_legacy != handlers_.end())
    {
        log_write_warning_information("TcpServer::execute_legacy_handler_for_tag [" + conn->name() + "] - No ProtocolHandler for tag '" + tag + "', falling back to OLD legacy Handler.");
        Handler legacy_handler = it_legacy->second;
        try
        {
            string response = legacy_handler(conn, buf);
            if (!response.empty())
                conn->send(response);
            return true;
        }
        catch (const exception &e)
        {
            log_write_error_information("Legacy handler (for tag '" + tag + "') exception on connection [" + conn->name() + "]: " + string(e.what()));
            conn->send("Internal server error (legacy handler exception).\r\n");
            return true;
        }
        catch (...)
        {
            log_write_error_information("Unknown legacy handler (for tag '" + tag + "') exception on connection [" + conn->name() + "].");
            conn->send("Unknown internal server error (legacy handler exception).\r\n");
            return true;
        }
    }
    return false;
}

void TcpServer::execute_default_protocol_handler(const ProtocolHandler &handler, const TcpConnectionPtr &conn, Buffer *buf, const string &tag, size_t header_len, uint32_t payload_len)
{
    log_write_warning_information("TcpServer::execute_default_protocol_handler [" + conn->name() + "] - Using NEW default_protocol_handler for tag '" + tag + "'.");
    ProtocolHandlerPair response;
    try
    {
        buf->retrieve(header_len);
        string_view payload_sv(buf->peek(), payload_len);
        size_t payload_to_consume = payload_len;
        response = handler(conn, tag, payload_sv);
        buf->retrieve(payload_to_consume);
    }
    catch (const exception &e)
    {
        log_write_error_information("Default ProtocolHandler exception for tag [" + tag + "] on connection [" + conn->name() + "]: " + string(e.what()));
        response.second = "Internal server error (default protocol handler exception).";
        if (buf->readable_bytes() >= payload_len)
        {
            buf->retrieve(payload_len);
        }
    }
    catch (...)
    {
        log_write_error_information("Unknown Default ProtocolHandler exception for tag [" + tag + "] on connection [" + conn->name() + "].");
        response.second = "Unknown internal server error (default protocol handler exception).";
        if (buf->readable_bytes() >= payload_len)
            buf->retrieve(payload_len);
    }
    if (!response.second.empty())
    {
        string packaged_response = TcpServer::package_message(response.first, response.second);
        if (!packaged_response.empty())
            conn->send(packaged_response);
    }
}

bool TcpServer::process_legacy_fallback(const TcpConnectionPtr &conn, Buffer *buf, size_t initial_readable)
{
    if (default_handler_)
    {
        try
        {
            string response = default_handler_(conn, buf);
            if (!response.empty())
                conn->send(response);
            if (buf->readable_bytes() < initial_readable)
                return true;
            else
            {
                if (initial_readable > 0)
                    log_write_warning_information("TcpServer::process_legacy_fallback [" + conn->name() + "] - OLD Legacy default handler did not consume any data.");
                return false;
            }
        }
        catch (const exception &e)
        {
            log_write_error_information("OLD Legacy default handler exception on connection [" + conn->name() + "]: " + string(e.what()));
            conn->send("Internal server error (legacy default handler exception).\r\n");
            return false;
        }
        catch (...)
        {
            log_write_error_information("Unknown OLD legacy default handler exception on connection [" + conn->name() + "].");
            conn->send("Unknown internal server error (legacy default handler exception).\r\n");
            return false;
        }
    }
    else
    {
        if (buf->readable_bytes() > 0)
        {
            log_write_warning_information("TcpServer::process_legacy_fallback [" + conn->name() + "] - No legacy default handler and data is not protocol format. Discarding " + to_string(buf->readable_bytes()) + " bytes.");
            buf->retrieve_all();
        }
        return false;
    }
}

void TcpServer::new_connection(int sockfd, const sockaddr_in &peer_addr)
{
    loop_->assert_in_loop_thread();

    char peer_ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));
    uint16_t peer_port = ntohs(peer_addr.sin_port);

    char buf[128];
    snprintf(buf, sizeof buf, "%s:%d#%d", peer_ip, peer_port, next_conn_id_);
    ++next_conn_id_;
    string conn_name = name_ + "-" + buf;

    log_write_regular_information("TcpServer::new_connection [" + name_ +
                                  "] - new connection [" + conn_name +
                                  "] from " + peer_ip + ":" + to_string(peer_port) +
                                  " sockfd=" + to_string(sockfd));

    sockaddr_in local_addr{};
    socklen_t addrlen = sizeof(local_addr);
    if (::getsockname(sockfd, reinterpret_cast<sockaddr *>(&local_addr), &addrlen) < 0)
    {
        log_write_error_information("TcpServer::new_connection - Failed to get local address for fd " + to_string(sockfd) + ": " + errno_to_string(errno));
        ::close(sockfd);
        return;
    }

    TcpConnectionPtr conn = make_shared<TcpConnection>(loop_, conn_name, sockfd, local_addr, peer_addr);
    connections_[conn_name] = conn;

    conn->set_connection_callback(connection_cb_);
    conn->set_message_callback(
        [this](const TcpConnectionPtr &c, Buffer *b)
        {
            log_write_regular_information("message at: " + c->name());
            return on_message(c, b);
        });
    conn->set_write_complete_callback(write_complete_cb_);
    conn->set_close_callback(
        [this](const TcpConnectionPtr &c)
        {
            log_write_regular_information("connection close: " + c->name());
            remove_connection(c);
        });

    loop_->run_in_loop([conn]()
                       {
        if (conn) conn->connect_established(); });
}

void TcpServer::remove_connection(const TcpConnectionPtr &conn)
{
    loop_->run_in_loop([this, conn]()
                       { remove_connection_in_loop(conn); });
}

void TcpServer::remove_connection_in_loop(const TcpConnectionPtr &conn)
{
    loop_->assert_in_loop_thread();
    if (!conn)
        return;

    log_write_regular_information("TcpServer::remove_connection_in_loop [" + name_ +
                                  "] - connection " + conn->name());

    size_t n = connections_.erase(conn->name());
    if (n != 1)
        log_write_warning_information("TcpServer::remove_connection_in_loop [" + name_ +
                                      "] - Tried to remove connection " + conn->name() + " but it was not found or removed multiple times.");

    loop_->queue_in_loop([conn]()
                         { conn->connect_destroyed(); });
}