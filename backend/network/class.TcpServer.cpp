#define _CHANNEL_CPP
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
      default_handler_{[](auto, auto)
                       { return string{"Unhandled request\r\n"}; }}
{
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
        loop_->run_in_loop([conn_copy]()
                           { conn_copy->connect_destroyed(); });
    }
    connections_.clear();
}

void TcpServer::register_handler(HandlerTag tag, Handler cb)
{
    handlers_[move(tag)] = move(cb);
    log_write_regular_information("Registered handler for tag: " + tag);
}

void TcpServer::set_default_handler(Handler cb)
{
    default_handler_ = move(cb);
    log_write_regular_information("Default handler set.");
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
                                  "] from " + peer_ip + ":" + to_string(peer_port));

    sockaddr_in local_addr{};
    socklen_t addrlen = sizeof(local_addr);
    if (::getsockname(sockfd, reinterpret_cast<sockaddr *>(&local_addr), &addrlen) < 0)
    {
        log_write_error_information("TcpServer::new_connection - Failed to get local address for fd " + to_string(sockfd) + ": " + errno_to_string(errno));
        ::close(sockfd);
        return;
    }

    TcpConnectionPtr conn = make_shared<TcpConnection>(loop_,
                                                       conn_name,
                                                       sockfd,
                                                       local_addr,
                                                       peer_addr);
    connections_[conn_name] = conn;

    conn->set_connection_callback(connection_cb_);
    conn->set_message_callback([this](const TcpConnectionPtr &c, Buffer *b)
                               { return on_message(c, b); });
    conn->set_write_complete_callback(write_complete_cb_);
    conn->set_close_callback([this](const TcpConnectionPtr &c)
                             { remove_connection(c); });

    loop_->run_in_loop([conn]()
                       { conn->connect_established(); });
}

void TcpServer::remove_connection(const TcpConnectionPtr &conn)
{
    loop_->run_in_loop([this, conn]()
                       { remove_connection_in_loop(conn); });
}

void TcpServer::remove_connection_in_loop(const TcpConnectionPtr &conn)
{
    loop_->assert_in_loop_thread();
    log_write_regular_information("TcpServer::remove_connection_in_loop [" + name_ +
                                  "] - connection " + conn->name());

    size_t n = connections_.erase(conn->name());
    assert(n == 1);

    loop_->queue_in_loop([conn]()
                         { conn->connect_destroyed(); });
}

string TcpServer::on_message(const TcpConnectionPtr &conn, Buffer *buf)
{
    if (buf->readable_bytes() < 1)
        return "";

    const uint8_t len = static_cast<uint8_t>(*buf->peek());

    if (buf->readable_bytes() < 1 + len)
    {
        return "";
    }

    string tag_with_len = buf->retrieve_as_string(1 + len);
    string tag = tag_with_len.substr(1); // Extract tag

    auto it = handlers_.find(tag);
    Handler handler_to_call = (it != handlers_.end()) ? it->second : default_handler_;

    try
    {
        return handler_to_call(conn, buf);
    }
    catch (const exception &e)
    {
        log_write_error_information("Handler exception for tag [" + tag + "] on connection [" + conn->name() + "]: " + string(e.what()));
        return "Internal server error.\r\n";
    }
    catch (...)
    {
        log_write_error_information("Unknown handler exception for tag [" + tag + "] on connection [" + conn->name() + "].");
        return "Internal server error.\r\n";
    }
}