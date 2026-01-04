#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <stddef.h>

class Buffer
{
public:
    static const size_t headerReservedSize_ = 8; // 预留头部空间大小
    static const size_t initialSize = 1024;      // 默认缓冲区大小
    explicit Buffer(size_t initialBufferSize = initialSize)
        : buffer_(headerReservedSize_ + initialBufferSize),
          readIndex_(headerReservedSize_),
          writeIndex_(headerReservedSize_)
    {
    }

    // 可读字节数
    size_t readableBytes() const { return writeIndex_ - readIndex_; }
    // 可写字节数
    size_t writeableBytes() const { return buffer_.size() - writeIndex_; }
    // 头部预留字节数
    size_t headerReservedBytes() const { return readIndex_; }

    // 返回可读数据的起始地址
    const char *readBeginAddr() const { return begin() + readIndex_; }
    // 返回可写数据的起始地址
    char *writeBeginAddr() { return begin() + writeIndex_; }

    // 移动readIndex_,表示数据已被读取
    void retrieve(size_t len);

    // 移动writeIndex_,表示数据已被写入
    void hasWritten(size_t len);

    // 重置缓冲区,未读完的数据直接丢失
    void reset();

    // 把所有数据转换为string返回
    std::string readAllAsString();

    // 确保有足够的可写空间，否则扩展缓冲区
    void ensureWritableBytes(size_t len);

    // 写入数据
    void append(const char *data, size_t len);
    void append(const std::string &str);

    ~Buffer() = default;

private:
    char *begin() { return buffer_.data(); }
    // 当 Buffer 对象是 const 时，只允许读，不允许通过返回指针修改内容，所以提供 const char* 返回值
    const char *begin() const { return buffer_.data(); }

    void makeSpace(size_t len)
    {
        if (writeableBytes() + headerReservedBytes() < len + headerReservedSize_)
        {
            // 扩展容量
            size_t targetSize = std::max(buffer_.capacity() * 2, writeIndex_ + len);
            buffer_.resize(targetSize);
        }
        else
        {
            // 搬移数据
            std::copy(begin() + readIndex_, begin() + writeIndex_, begin() + headerReservedSize_);
            readIndex_ = headerReservedSize_;
            writeIndex_ = readIndex_ + readableBytes();
        }
    }

    std::vector<char> buffer_; // 底层缓冲区
    size_t readIndex_;         // 读指针
    size_t writeIndex_;        // 写指针
};