#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "Socket.hpp"
#include "InetAddress.hpp"

Socket::~Socket()
{
    // 在这里真正的关闭连接socket或关闭监听socket并释放资源，TcpConnection中的断开连接只是逻辑上的断开
    closeFd();
}

void Socket::bindAddress(const InetAddress &localaddr)
{
    if (0 != ::bind(sockfd_, (sockaddr *)&localaddr.getSockAddrIn(), sizeof(sockaddr_in)))
    {
        // 绑定失败，打印错误信息并退出
        perror("Socket::bindAddress failed");
        ::close(sockfd_);
        exit(EXIT_FAILURE);
    }
}

void Socket::listen()
{
    if (0 != ::listen(sockfd_, SOMAXCONN))
    {
        // 监听失败，打印错误信息并退出
        perror("Socket::listen failed");
        ::close(sockfd_);
        exit(EXIT_FAILURE);
    }
}

int Socket::accept(InetAddress *peeraddr)
{
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    // 使用 accept4 接受连接，并原子地设置非阻塞和关闭时关闭标志（CLOEXEC：用于放置父进程在调用fork创建子进程弧，子进程继承父进程打开的文件描述符，而在父进程中关闭该文件描述符后，子进程仍然持有，可能导致死锁，设置该标志使内核会在子进程执行 exec 替换内存空间时，自动关闭这个文件描述符。）
    // accept4参数说明：

    int connfd = ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        // 成功接受连接，保存对端地址信息，避免使用时频繁调用 getpeername 系统调用，减少一次系统调用开销
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        perror("Socket::shutdownWrite failed");
    }
}

InetAddress Socket::getLocalAddress() const
{
    struct sockaddr_in localaddr;
    memset(&localaddr, 0, sizeof localaddr);
    socklen_t addrlen = sizeof(localaddr);

    // 核心系统调用，连接socket的本地地址由三次握手之后内核自动确定，一般会继承监听socket的本地地址和端口
    if (::getsockname(sockfd_, (struct sockaddr *)&localaddr, &addrlen) < 0)
    {
        perror("Socket::getLocalAddress");
    }

    return InetAddress(localaddr);
}

InetAddress Socket::getPeerAddress() const
{
    struct sockaddr_in peeraddr;
    memset(&peeraddr, 0, sizeof peeraddr);
    socklen_t addrlen = sizeof(peeraddr);

    // 核心系统调用
    if (::getpeername(sockfd_, (struct sockaddr *)&peeraddr, &addrlen) < 0)
    {
        perror("Socket::getPeerAddress");
    }

    return InetAddress(peeraddr);
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

void Socket::closeFd()
{
    if (sockfd_ >= 0)
    {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

void Socket::reset()
{
    closeFd();
}
