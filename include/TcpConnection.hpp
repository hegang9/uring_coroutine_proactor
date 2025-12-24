#pragma once

#include <atomic>
#include <memory>

#include "Noncopyable.hpp"
#include "Buffer.hpp"
#include "Socket.hpp"
#include "InetAddress.hpp"

class EventLoop;

// TCP连接状态枚举
enum class TcpConnectionState
{
    kDisconnected = 0, // 断开连接
    kConnecting = 1,   // 连接中
    kConnected = 2,    // 已连接
    kDisconnecting = 3 // 断开中
};

class TcpConnection : private Noncopyable
{
private:
    EventLoop *loop_;                       // 所属的 子EventLoop
    Socket socket_;                         // 连接的Socket对象
    std::atomic<TcpConnectionState> state_; // 连接状态

    bool reading_; // 是否处于读状态

    Buffer inputBuffer_;
    Buffer outputBuffer_;

    const InetAddress localAddr_; // 本地地址
    const InetAddress peerAddr_;  // 对端地址，保存下来以免频繁调用

    // 回调函数对象
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

public:
    // 连接建立/断开回调
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    // 消息到来回调
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection> &, Buffer &)>;
    // 写完成回调
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    // 高水位回调
    using HighWaterMarkCallback = std::function<void(const std::shared_ptr<TcpConnection> &, size_t)>;
    // 内部回调，通知 Server移除自己
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;

    TcpConnection(EventLoop *loop, int sockfd, const InetAddress &peerAddr);
    ~TcpConnection();

    // 改变连接状态
    void setState(TcpConnectionState state);

    // 获取所属的 EventLoop
    EventLoop *getLoop() const { return loop_; };

    // 获取本地地址
    InetAddress getLocalAddr() const { return localAddr_; }

    // 获取对端地址
    InetAddress getPeerAddr() const { return peerAddr_; }

    // 优雅关闭
    void shutdown();
};