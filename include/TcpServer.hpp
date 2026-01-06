#pragma once

#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <atomic>

#include "EventLoop.hpp"
#include "Acceptor.hpp"
#include "InetAddress.hpp"
#include "TcpConnection.hpp"
#include "EventLoopThreadPool.hpp"

/**
 * @file TcpServer.hpp
 * 网络库对外的核心入口类，用于管理服务器的生命周期和连接的生命周期
 * 对于使用者来说，他们只需要配置 TcpServer（监听端口、设置回调），然后调用 start()，剩下的底层细节（Socket 创建、Accept、连接管理、IO 循环）都由 TcpServer 内部处理
 * 核心功能：
 * 创建并管理Acceptor对象，监听新连接
 * 管理TcpConnection对象，维护活动连接列表
 * 调度线程池，每个线程运行一个EventLoop
 * 提供设置连接回调、消息回调等接口
 */

class TcpServer
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;

    TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &name = "TcpServer");
    ~TcpServer();
    // 禁用拷贝和赋值
    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    void start();                      // 启动服务器，开始监听新连接
    void setThreadNum(int numThreads); // 设置工作线程数量
    // 设置新连接回调函数
    void setConnectionCallback(const TcpConnection::ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }

private:
    void newConnection(int sockfd, const InetAddress &peerAddr);       // 新连接到来时的回调函数
    void removeConnection(const std::shared_ptr<TcpConnection> &conn); // 连接断开时的回调函数

    EventLoop *loop_;                       // 主线程的 EventLoop 对象，负责监听和接受新连接
    const std::string name_;                // 服务器名称
    const std::string ipPort_;              // 服务器监听的地址和端口字符串表示
    std::unique_ptr<Acceptor> acceptor_;    // 负责监听和接受新连接的 Acceptor 对象
    ConnectionCallback connectionCallback_; // 用户设置的新连接回调
    std::atomic_bool started_;              // 服务器是否已启动

    int nextConnId_; // 下一个连接的 ID，用于生成唯一连接名称

    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> connections_; // 活动连接列表，key是连接名称，value是 TcpConnection 对象，使用shared_ptr保证连接在断开前不被析构
    EventLoopThreadPool threadPool_;                                              // 线程池，每个线程运行一个 EventLoop
};