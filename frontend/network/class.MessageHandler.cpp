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

#define _CLASS_MESSAGEHANDLER_CPP
#include "network.hpp"

void ClientSocket::MessageHandler::process_received_data(Buffer &recv_buffer)
{
    while (true)
    {
        if (recv_buffer.readable_bytes() < 1)
            break;
        const uint8_t tag_len = static_cast<uint8_t>(*recv_buffer.peek());
        const size_t header_len = 1 + tag_len + sizeof(uint32_t);
        if (recv_buffer.readable_bytes() < header_len)
            break;
        uint32_t payload_len_net;
        std::memcpy(&payload_len_net, recv_buffer.peek() + 1 + tag_len, sizeof(payload_len_net));
        uint32_t payload_len = ntohl(payload_len_net);
        if (payload_len > Buffer::kMaxFrameSize)
        {
            log_write_error_information("Received frame payload length (" + std::to_string(payload_len) + ") exceeds limit (" + std::to_string(Buffer::kMaxFrameSize) + ").");
            owner_.trigger_error_callback_internal("Received frame too large.");
            owner_.request_disconnect_async_internal("Protocol error: frame too large");
            recv_buffer.retrieve_all();
            return;
        }
        const size_t total_message_len = header_len + payload_len;
        if (recv_buffer.readable_bytes() < total_message_len)
            break;
        std::string tag(recv_buffer.peek() + 1, tag_len);
        recv_buffer.retrieve(header_len);
        std::string payload = recv_buffer.retrieve_as_string(payload_len);
        Handler handler_to_call;
        bool found_handler = false;
        {
            std::shared_lock lock(handler_rw_mutex_);
            auto it = handlers_.find(tag);
            if (it != handlers_.end())
            {
                handler_to_call = it->second;
                found_handler = true;
            }
            else if (default_handler_)
            {
                handler_to_call = default_handler_;
                found_handler = true;
            }
        }
        if (found_handler && handler_to_call)
            owner_.thread_pool_.enqueue(0, [h = std::move(handler_to_call), p = std::move(payload), tag_copy = tag]() mutable
                                        { try { h(p); } catch (const std::exception& e) { log_write_error_information("Handler for tag '" + tag_copy + "' threw an exception: " + std::string(e.what())); } catch (...) { log_write_error_information("Handler for tag '" + tag_copy + "' threw an unknown exception."); } });
        else
            log_write_warning_information("No handler found for tag '" + tag + "' and no default handler set. Discarding message payload (size " + std::to_string(payload.length()) + ").");
    }
}