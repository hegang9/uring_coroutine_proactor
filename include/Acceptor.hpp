#pragma once

#include <functional>

#include "Socket.hpp"
#include "IoContext.hpp"
#include <netinet/in.h>
#include <memory>

class EventLoop;
class InetAddress;

/**
 * 负责监听（创建监听Socket）和接受新连接的类，对象属于 main Proactor线程。
 */

class Acceptor
{
public:
    // 禁止拷贝和赋值
    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;

    // 新连接回调函数类型，参数是新连接的socket文件描述符和对端地址，用于封装sockfd为一个 TcpConnection 对象
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &peerAddr)>;

    /**
     * loop: 所属的EventLoop对象，监听Socket属于main Proactor线程
     * listenAddr: 监听地址和端口
     * reuseport: 是否开启端口复用
     */
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    // 设置新连接到来的回调函数(在TcpServer中设置)
    void setNewConnectionCallback(const NewConnectionCallback &cb)
    {
        newConnectionCallback_ = cb;
    }

    // 判断是否在监听
    bool isListening() const { return listening_; }
    // 监听本地端口
    void listen();

private:
    void handleRead(int res); // 监听Socket可读事件的回调函数，接受新连接
    void asyncAccept();       // 提交异步 accept 请求

    EventLoop *acceptLoop_;                       // 所属的EventLoop对象，监听Socket属于main Proactor线程
    Socket listenSocket_;                         // 监听Socket
    bool listening_;                              // 是否正在监听
    NewConnectionCallback newConnectionCallback_; // 新连接到来的回调函数

    // 用于 io_uring accept 的缓冲区
    struct sockaddr_in clientAddr_;
    socklen_t clientAddrLen_;
    IoContext acceptContext_; // 专门用于 accept 的上下文
};
