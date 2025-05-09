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

#define _CLASS_BUFFER_CPP
#include "network.hpp"
using namespace net;

void Buffer::make_space(size_t len)
{
    if (writable_bytes() + prependable_bytes() < len + kCheapPrepend)
        buffer_.resize(writer_index_ + len);
    else
    {
        size_t readable = readable_bytes();
        memmove(begin() + kCheapPrepend, peek(), readable);
        reader_index_ = kCheapPrepend;
        writer_index_ = reader_index_ + readable;
    }
    assert(writable_bytes() >= len);
}

ssize_t Buffer::read_fd(int fd, int *saved_errno)
{
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writable_bytes();
    vec[0].iov_base = begin_write();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const int iovcnt = 2;

    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0)
    {
        *saved_errno = errno;
        return -1;
    }
    else if (n == 0)
        return 0;
    else if (static_cast<size_t>(n) <= writable)
        has_written(n);
    else
    {
        if (static_cast<size_t>(n) > writable + sizeof(extrabuf))
        {
            log_write_error_information("Buffer::read_fd read more data than buffers could hold!");
            *saved_errno = EOVERFLOW;
            return -1;
        }
        size_t must_write_from_extrabuf = static_cast<size_t>(n) - writable;
        writer_index_ = buffer_.size();
        append(extrabuf, must_write_from_extrabuf);
    }
    return n;
}
