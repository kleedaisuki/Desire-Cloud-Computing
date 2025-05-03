#define _CLASS_CLIENTSOCKET_CPP
#include "network.hpp"

ClientSocket::ClientSocket(string server_ip, uint16_t server_port)
    : server_ip_(move(server_ip)),
      server_port_(server_port),
      sockfd_(-1),
      is_connected_(false),
      stop_requested_(true),
      thread_pool_(ThreadPool::instance()),
      connection_manager_(make_unique<ConnectionManager>(*this)),
      sender_(make_unique<Sender>(*this)),
      receiver_(make_unique<Receiver>(*this)),
      message_handler_(make_unique<MessageHandler>(*this))
{
    log_write_regular_information("ClientSocket components initialized for " + server_ip_ + ":" + to_string(server_port_));
    if (!connect())
        log_write_warning_information("Initial connection attempt failed during construction.");
}

ClientSocket::~ClientSocket()
{
    log_write_regular_information("ClientSocket destructor called.");
    disconnect();
}

bool ClientSocket::connect()
{
    lock_guard<mutex> lock(connection_mutex_);
    return connect_internal();
}

void ClientSocket::disconnect()
{
    lock_guard<mutex> lock(connection_mutex_);
    disconnect_internal();
}

bool ClientSocket::connect_internal()
{
    if (is_connected_.load(memory_order_relaxed))
        return true;
    disconnect_internal();

    log_write_regular_information("Attempting internal connection...");
    int new_sockfd = connection_manager_->try_connect();
    if (new_sockfd == -1)
    {
        log_write_error_information("Internal connection attempt failed.");
        sockfd_.store(-1, memory_order_relaxed);
        is_connected_.store(false, memory_order_relaxed);
        stop_requested_.store(true, memory_order_relaxed);
        return false;
    }

    log_write_regular_information("Socket connected successfully (fd=" + to_string(new_sockfd) + ").");
    sockfd_.store(new_sockfd, memory_order_release);
    stop_requested_.store(false, memory_order_release);
    is_connected_.store(true, memory_order_release);

    if (!start_io_threads())
    {
        log_write_error_information("Failed to start IO threads after connection.");

        is_connected_.store(false, memory_order_relaxed);
        stop_requested_.store(true, memory_order_relaxed);
        int fd_to_close = sockfd_.exchange(-1, memory_order_acq_rel);
        if (fd_to_close != -1)
            connection_manager_->close_socket(fd_to_close);
        return false;
    }

    log_write_regular_information("IO threads started.");
    trigger_connection_callback_internal(true);
    return true;
}

void ClientSocket::disconnect_internal()
{
    int current_sockfd = sockfd_.load(memory_order_relaxed);
    if (current_sockfd == -1 and !send_thread_.joinable() and !recv_thread_.joinable())
    {
        is_connected_.store(false, memory_order_relaxed);
        stop_requested_.store(true, memory_order_relaxed);
        return;
    }

    bool previously_stopped = stop_requested_.exchange(true);
    if (!previously_stopped and sender_)
        sender_->notify_sender();

    int fd_to_close = sockfd_.exchange(-1, memory_order_acq_rel);
    if (fd_to_close != -1 and connection_manager_)
        connection_manager_->close_socket(fd_to_close);

    stop_and_join_io_threads();

    if (sender_)
        sender_->clear_queue();
    if (receiver_)
        receiver_->clear_buffer();

    bool was_connected = is_connected_.exchange(false);
    if (was_connected)
        trigger_connection_callback_internal(false);
}

bool ClientSocket::send_message(const string &tag, string_view payload)
{
    if (!is_connected_.load(memory_order_relaxed))
    {
        log_write_warning_information("Cannot send message: Not connected.");
        return false;
    }
    if (!sender_)
    {
        log_write_error_information("Cannot send message: Sender component is not initialized.");
        return false;
    }
    if (tag.length() > numeric_limits<uint8_t>::max())
    {
        log_write_warning_information("Tag too long");
        return false;
    }
    if (payload.length() > numeric_limits<uint32_t>::max())
    {
        log_write_warning_information("Payload too long");
        return false;
    }
    uint8_t tag_len = static_cast<uint8_t>(tag.length());
    uint32_t payload_len_net = htonl(static_cast<uint32_t>(payload.length()));
    vector<char> message;
    message.reserve(1 + tag_len + sizeof(payload_len_net) + payload.length());
    message.push_back(static_cast<char>(tag_len));
    message.insert(message.end(), tag.begin(), tag.end());
    const char *len_bytes = reinterpret_cast<const char *>(&payload_len_net);
    message.insert(message.end(), len_bytes, len_bytes + sizeof(payload_len_net));
    message.insert(message.end(), payload.begin(), payload.end());
    sender_->enqueue_message(move(message));
    return true;
}
bool ClientSocket::send_text(const string &tag, const string &text_payload) { return send_message(tag, text_payload); }
bool ClientSocket::send_binary(const string &tag, const vector<char> &binary_payload) { return send_message(tag, string_view(binary_payload.data(), binary_payload.size())); }

bool ClientSocket::send_file(const string &tag, const string &file_path, size_t chunk_size)
{
    ifstream ifs(file_path, ios::binary | ios::ate);
    if (!ifs)
    {
        log_write_error_information("Failed to open file for sending: " + file_path);
        return false;
    }
    streamsize file_size = ifs.tellg();
    ifs.seekg(0, ios::beg);
    if (file_size == 0)
        return send_message(tag, "");
    if (file_size < 0)
    {
        log_write_error_information("Failed to get file size or file not seekable: " + file_path);
        return false;
    }
    vector<char> buffer(chunk_size);
    while (ifs and is_connected())
    {
        ifs.read(buffer.data(), buffer.size());
        streamsize bytes_read = ifs.gcount();
        if (bytes_read <= 0)
            break;
        if (!send_message(tag, string_view(buffer.data(), static_cast<size_t>(bytes_read))))
        {
            log_write_error_information("Failed to send file chunk for: " + file_path);
            return false;
        }
    }
    if (ifs.bad())
    {
        log_write_error_information("Error reading file: " + file_path);
        return false;
    }
    return is_connected();
}
void ClientSocket::register_handler(const string &tag, Handler handler)
{
    if (message_handler_)
        message_handler_->register_handler(tag, move(handler));
}
void ClientSocket::register_default_handler(Handler handler)
{
    if (message_handler_)
        message_handler_->register_default_handler(move(handler));
}

void ClientSocket::register_connection_callback(ConnectionCallback cb) { connection_cb_ = move(cb); }
void ClientSocket::register_error_callback(ErrorCallback cb) { error_cb_ = move(cb); }


void ClientSocket::trigger_error_callback_internal(const string &error_msg)
{
    if (error_cb_)
    {
        auto cb = error_cb_;
        thread_pool_.enqueue(0, [cb, msg = error_msg]()
                             { try { cb(msg); } catch (...) { log_write_error_information("Exception caught in error callback."); } });
    }
}

void ClientSocket::trigger_connection_callback_internal(bool connected)
{
    if (connection_cb_)
    {
        auto cb = connection_cb_;
        thread_pool_.enqueue(0, [cb, conn = connected]()
                             { try { cb(conn); } catch (...) { log_write_error_information("Exception caught in connection callback."); } });
    }
}

void ClientSocket::request_disconnect_async_internal(const string &reason)
{
    if (!stop_requested_.exchange(true))
    {
        log_write_regular_information("Async disconnect requested due to: " + reason);
        if (sender_)
            sender_->notify_sender();
    }
}

bool ClientSocket::start_io_threads()
{
    if (!sender_ or !receiver_)
    {
        log_write_error_information("Cannot start IO threads: Sender or Receiver component is null.");
        return false;
    }
    try
    {
        send_thread_ = thread(&Sender::send_loop, sender_.get());
        recv_thread_ = thread(&Receiver::recv_loop, receiver_.get());
        return true;
    }
    catch (const system_error &e)
    {
        log_write_error_information("Failed to start IO threads: " + string(e.what()));
        stop_requested_ = true;
        return false;
    }
}
void ClientSocket::stop_and_join_io_threads()
{
    if (send_thread_.joinable())
    {
        try
        {
            send_thread_.join();
        }
        catch (const system_error &e)
        {
            log_write_error_information("Exception caught joining send thread: " + string(e.what()));
        }
    }
    if (recv_thread_.joinable())
    {
        try
        {
            recv_thread_.join();
        }
        catch (const system_error &e)
        {
            log_write_error_information("Exception caught joining receive thread: " + string(e.what()));
        }
    }
}