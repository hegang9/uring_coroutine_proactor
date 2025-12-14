#pragma once

#include "Noncopyable.hpp"

/**
 * 封装 socket 相关操作的类。
 */

// 前向声明，降低耦合
class InetAddress;

class Socket : private Noncopyable
{
public:
    // 显式构造函数，explicit 防止隐式转换
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    ~Socket();

    int getFd() const { return sockfd_; }

    // 绑定本机地址
    void bindAddress(const InetAddress &localaddr);
    // 监听本机端口，准备接受连接
    void listen();
    // 接收新连接，返回新连接的socket文件描述符
    int accept(InetAddress *peeraddr);
    // 半关闭写端，发送FIN包，用于优雅关闭
    void shutdownWrite();

    // 禁用Nagle算法（为了减少网络拥塞，它会把多个小的写操作合并成一个大的 TCP 包发送。这会导致小数据包发送有延迟），提高实时性
    void setTcpNoDelay(bool on);
    // 允许重用处于TIME_WAIT状态的地址和端口，避免绑定失败。当服务器重启时，之前的监听端口可能还处于 TIME_WAIT 状态。如果不开启此选项，重启服务器会报错 "Address already in use"，必须等几分钟才能启动。对于服务器监听 Socket (ListenFd)，必须设置为 true
    void setReuseAddr(bool on);
    // 允许多个Socket绑定同一个地址和端口，通常用于多线程服务器模型，提高并发处理能力。内核会自动在这些进程间进行负载均衡（通常基于哈希），主从模型不需要设置为 true。在此项目中不需要使用多进程模型，因此不需要使用此选项
    void setReusePort(bool on);
    // 启用 TCP KeepAlive 机制，定期发送探测包以检测连接是否仍然有效，防止死连接占用资源。内核默认的心跳周期太长（2小时），对于即时通讯或高并发 Web 服务器来说太慢了。通常应用层会自己实现一套心跳机制（Application Level Heartbeat），比如每 30 秒发一个空包。
    void setKeepAlive(bool on);

private:
    int sockfd_;
};