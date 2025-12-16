#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Acceptor.hpp"
#include "InetAddress.hpp"

// 创建一个非阻塞的监听Socket文件描述符
static int createNonblockingSocket()
{
    int listenSocketFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listenSocketFd < 0)
    {
        perror("socket");
        abort();
    }
    return listenSocketFd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop),
      listenSocket_(createNonblockingSocket()),
      listening_(false)
{
    // 允许地址重用，防止TIME_WAIT导致绑定失败
    listenSocket_.setReuseAddr(true);
    // 端口重用，为以后扩展多线程acceptor做准备
    listenSocket_.setReusePort(reuseport);
    // 绑定监听地址
    listenSocket_.bindAddress(listenAddr);
}

Acceptor::~Acceptor()
{
    if (listening_)
    {
        ::close(listenSocket_.getFd());
    }
}

void Acceptor::listen()
{
    listening_ = true;
    listenSocket_.listen();
}

void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = listenSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr); // 执行新连接处理回调函数
        }
        else
        {
            ::close(connfd); // 没有回调函数，关闭连接
        }
    }
    else
    {
        perror("Acceptor::handleRead");
        if (errno == EMFILE)
        {
            // 文件描述符达到上限，记录日志
        }
    }
}
