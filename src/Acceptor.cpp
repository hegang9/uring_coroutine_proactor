#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <liburing.h>

#include "Acceptor.hpp"
#include "InetAddress.hpp"
#include "EventLoop.hpp"

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
    : acceptLoop_(loop),
      listenSocket_(createNonblockingSocket()),
      listening_(false),
      clientAddrLen_(sizeof(clientAddr_)),
      acceptContext_(IoType::Accept, listenSocket_.getFd())
{
    // 允许地址重用，防止TIME_WAIT导致绑定失败
    listenSocket_.setReuseAddr(true);
    // 端口重用，为以后扩展多线程acceptor做准备
    listenSocket_.setReusePort(reuseport);
    // 绑定监听地址
    listenSocket_.bindAddress(listenAddr);

    // 绑定回调函数
    acceptContext_.handler = std::bind(&Acceptor::handleRead, this, std::placeholders::_1);
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
    asyncAccept(); // 提交第一个 accept 请求
}

void Acceptor::asyncAccept()
{
    // 获取 SQE
    struct io_uring_sqe *sqe = io_uring_get_sqe(&(acceptLoop_->ring_));
    if (!sqe)
    {
        // 如果 SQ 满了，可能需要处理错误或重试
        // 这里简单处理，实际项目中可能需要更健壮的错误处理
        perror("No SQE available");
        return;
    }

    // 准备 ACCEPT 操作
    io_uring_prep_accept(sqe, listenSocket_.getFd(), (struct sockaddr *)&clientAddr_, &clientAddrLen_, 0);

    // 绑定上下文
    io_uring_sqe_set_data(sqe, &acceptContext_);

    // 提交
    io_uring_submit(&(acceptLoop_->ring_));
}

void Acceptor::handleRead(int res)
{
    // res 是 accept 的返回值，即新的 connfd
    if (res >= 0)
    {
        int connfd = res;
        InetAddress peerAddr(clientAddr_);
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
        // accept 失败
        // 如果是 ECANCELED，说明可能是 EventLoop 正在退出
        if (res != -ECANCELED)
        {
            errno = -res;
            perror("Acceptor::handleRead");
        }
    }

    // 只要还在监听状态，就必须继续提交下一个 accept 请求
    if (listening_)
    {
        // 重置 clientAddrLen_，因为 accept 可能会修改它
        clientAddrLen_ = sizeof(clientAddr_);
        asyncAccept();
    }
}
