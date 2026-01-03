#include "Buffer.hpp"

void Buffer::retrieve(size_t len)
{
    // 先判断是否越界
    if (len <= readableBytes())
    {
        readIndex_ += len;
    }
    else
    {
        // 越界则全部读取完
        readIndex_ = writeIndex_;
    }
}

void Buffer::reset()
{
    readIndex_ = headerReservedSize_;
    writeIndex_ = headerReservedSize_;
}

std::string Buffer::readAllAsString()
{
    std::string result(readBeginAddr(), readableBytes());
    reset();
    return result;
}

void Buffer::ensureWritableBytes(size_t len)
{
    if (writeableBytes() < len)
    {
        makeSpace(len);
    }
}

void Buffer::append(const char *data, size_t len)
{
    ensureWritableBytes(len);
    std::copy(data, data + len, begin() + writeIndex_);
    writeIndex_ += len;
}

void Buffer::append(const std::string &str)
{
    append(str.data(), str.size());
}