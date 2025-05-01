#define _CHANNEL_CPP
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

    vec[0].iov_base = begin() + writer_index_;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0)
        *saved_errno = errno;
    else if (static_cast<size_t>(n) <= writable)
        writer_index_ += n;
    else
        writer_index_ = buffer_.size();
        append(extrabuf, n - writable);
    return n;
}
