#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "AsyncRead.hpp"
#include "AsyncWrite.hpp"
#include "Buffer.hpp"
#include "CoroutineTask.hpp"
#include "EventLoop.hpp"
#include "InetAddress.hpp"
#include "IoContext.hpp"
#include "MemoryPool.hpp"
#include "Socket.hpp"

// TCP连接状态枚举
enum class TcpConnectionState
{
    kDisconnected, // 断开连接
    kConnecting,   // 连接中
    kConnected,    // 已连接
    kDisconnecting // 断开中
};

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
  public:
    // 连接建立/断开回调
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    // 消息到来回调
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;

    TcpConnection(const std::string &name, EventLoop *loop, int sockfd, const InetAddress &peerAddr);
    ~TcpConnection();

    // 禁用拷贝和赋值
    TcpConnection(const TcpConnection &) = delete;
    TcpConnection &operator=(const TcpConnection &) = delete;

    // 设置回调函数
    void setConnectionCallback(const ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }
    void setCloseCallback(const CloseCallback &cb)
    {
        closeCallback_ = cb;
    }

    // 改变连接状态
    void setState(TcpConnectionState state);

    bool isConnected() const
    {
        return state_.load() == TcpConnectionState::kConnected;
    }
    bool isDisconnecting() const
    {
        return state_.load() == TcpConnectionState::kDisconnecting;
    }

    // 重置TcpConnection，防止复用内存池中的内存时还残留上一个TcpConnection的脏数据
    void reset();

    // 获取所属的 EventLoop
    EventLoop *getLoop() const
    {
        return loop_;
    };

    // 获取本地地址
    InetAddress getLocalAddr() const
    {
        return localAddr_;
    }

    // 获取对端地址
    InetAddress getPeerAddr() const
    {
        return peerAddr_;
    }

    // 获取连接名称
    const std::string &getName() const
    {
        return name_;
    }

    // 发送FIN包，半关闭写端
    void shutdown();
    // 强制关闭连接
    void forceClose();

    // 断开连接处理函数
    void handleClose();

    // 提交异步读写操作到io_uring
    void submitReadRequest(size_t nbytes);
    void submitReadRequestWithUserBuffer(char *userBuf, size_t userBufCap, size_t nbytes);
    void submitWriteRequest();
    void submitWriteRequestWithRegBuffer(void *buf, size_t len, int idx);

    // 设置超时时间
    void setTimeout(std::chrono::milliseconds timeout);

    // 异步读写操作的协程接口，创建 Awaitable
    // 对象（这个Awaitable对象包含了读或写操作所需的所有参数），当使用co_await时会触发await_suspend提交io_uring请求
    AsyncReadAwaitable asyncRead(size_t len)
    {
        return AsyncReadAwaitable(this, len);
    };
    AsyncReadAwaitable asyncRead(char *userBuf, size_t userBufCap, size_t len)
    {
        return AsyncReadAwaitable(this, userBuf, userBufCap, len);
    };
    AsyncWriteAwaitable asyncWrite()
    {
        return AsyncWriteAwaitable(this);
    };

    // 发送数据
    AsyncWriteAwaitable asyncSend(const std::string &data)
    {
        outputBuffer_.append(data);
        return asyncWrite();
    }

    AsyncWriteAwaitable asyncSend(const char *data, size_t len)
    {
        outputBuffer_.append(data, len);
        return asyncWrite();
    }

    // 零拷贝发送：直接从已注册缓冲区发送数据，不经过 outputBuffer_
    // 通常用于 Echo 等场景：读到的数据直接原样发回
    AsyncWriteAwaitable asyncSendZeroCopy()
    {
        // 使用当前读缓冲区的数据直接发送
        return AsyncWriteAwaitable(this, curReadBuffer_, curReadBufferSize_, readContext_.idx);
    }

    // 提供获取IoContext的接口
    IoContext &getReadContext()
    {
        return readContext_;
    }
    IoContext &getWriteContext()
    {
        return writeContext_;
    }

    // 提供获取输入缓冲区的接口
    void *&getCurReadBuffer()
    {
        return curReadBuffer_;
    }
    size_t &getCurReadBufferSize()
    {
        return curReadBufferSize_;
    }
    size_t &getCurReadBufferOffset()
    {
        return curReadBufferOffset_;
    }
    void setCurReadBuffer(void *buf)
    {
        curReadBuffer_ = buf;
    }
    void setCurReadBufferSize(size_t size)
    {
        curReadBufferSize_ = size;
    }
    void setCurReadBufferOffset(size_t offset)
    {
        curReadBufferOffset_ = offset;
    }

    // 释放当前读缓冲区（如果使用了已注册缓冲区，则归还）
    void releaseCurReadBuffer();

    // 提供获取输入缓冲区的统一接口（无论是固定缓冲区还是用户缓冲区）
    std::pair<const char *, size_t> getDataFromBuffer() const
    {
        return {static_cast<const char *>(curReadBuffer_), curReadBufferSize_};
    }

    // 提供获取Buffer的接口
    Buffer &getOutputBuffer()
    {
        return outputBuffer_;
    }

    // 初始化连接（在所属 Loop 执行）
    void connectEstablished();
    // 销毁连接（在所属 Loop 执行）
    void connectDestroyed();

  private:
    EventLoop *loop_;                       // 所属的 子EventLoop
    Socket socket_;                         // 连接的Socket对象
    std::atomic<TcpConnectionState> state_; // 连接状态
    std::string name_;                      // 连接名称

    std::atomic_bool closeCallbackInvoked_{false}; // 防止关闭回调被重复触发的保护位

    bool reading_; // 是否处于读状态

    IoContext readContext_;                 // 读操作的上下文
    IoContext writeContext_;                // 写操作的上下文
    IoContext timeoutContext_;              // 超时操作的上下文
    std::chrono::milliseconds readTimeout_; // 读超时时间
    __kernel_timespec readTimeoutSpec_;     // 读超时的内核时间结构体

    // 存储当前读操作使用的输入缓冲区信息，可以为固定缓冲区也可以为用户提供的缓冲区
    void *curReadBuffer_;        // 当前读缓冲区指针
    size_t curReadBufferSize_;   // 当前读缓冲区的有效数据大小
    size_t curReadBufferOffset_; // 当前读缓冲区的偏移位置
    Buffer outputBuffer_;        // 发送缓冲区

    const InetAddress localAddr_; // 本地地址
    const InetAddress peerAddr_;  // 对端地址，保存下来以免频繁调用

    // 回调函数对象
    ConnectionCallback connectionCallback_;
    CloseCallback closeCallback_;
};