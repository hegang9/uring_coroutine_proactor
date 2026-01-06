#include "TcpConnection.hpp"
#include <cstring>
#include <unistd.h>

TcpConnection::TcpConnection(const std::string &name, EventLoop *loop, int sockfd, const InetAddress &peerAddr)
    : name_(name),
      loop_(loop),
      socket_(sockfd),
      state_(TcpConnectionState::kDisconnected),
      reading_(false),
      inputBuffer_(),
      outputBuffer_(),
      readContext_(IoType::Read, sockfd),
      writeContext_(IoType::Write, sockfd),
      localAddr_(socket_.getLocalAddress()),
      peerAddr_(peerAddr),
      connectionCallback_(nullptr),
      closeCallback_(nullptr)
{
    // 协程模式下，不需要绑定传统的回调函数 (handleRead/handleWrite)
}

TcpConnection::~TcpConnection()
{
    // 逻辑关闭连接，调用socket的析构函数释放资源
}

void TcpConnection::setState(TcpConnectionState state)
{
    state_.store(state);
}

void TcpConnection::shutdown()
{
    // 发送FIN包，半关闭写端
    if (state_.load() == TcpConnectionState::kConnected)
    {
        setState(TcpConnectionState::kDisconnecting);
        socket_.shutdownWrite();
    }
}

void TcpConnection::forceClose()
{
    TcpConnectionState expected = TcpConnectionState::kConnected;
    if (state_.compare_exchange_strong(expected, TcpConnectionState::kDisconnecting))
    {
        loop_->queueInLoop(std::bind(&TcpConnection::handleClose, shared_from_this()));
    }
    // 如果已经是 kDisconnecting，不重复提交
}

void TcpConnection::submitReadRequest(size_t nbytes)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        fprintf(stderr, "TcpConnection::submitReadRequest: SQ full\n");
        return;
    }

    // 准备读取nbytes字节的数据到 inputBuffer_
    inputBuffer_.ensureWritableBytes(nbytes); // 确保有足够的可写空间
    io_uring_prep_read(sqe, socket_.getFd(), inputBuffer_.writeBeginAddr(), nbytes, 0);
    io_uring_sqe_set_data(sqe, &readContext_);
    int ret = io_uring_submit(&loop_->ring_);
    if (ret < 0)
    {
        fprintf(stderr, "TcpConnection::submitReadRequest: io_uring_submit failed: %s\n", strerror(-ret));
    }
}

void TcpConnection::submitWriteRequest()
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        fprintf(stderr, "TcpConnection::submitWriteRequest: SQ full\n");
        return;
    }

    // 准备写出 outputBuffer_ 中的数据
    io_uring_prep_write(sqe, socket_.getFd(), outputBuffer_.readBeginAddr(), outputBuffer_.readableBytes(), 0);
    io_uring_sqe_set_data(sqe, &writeContext_);
    int ret = io_uring_submit(&loop_->ring_);
    if (ret < 0)
    {
        fprintf(stderr, "TcpConnection::submitWriteRequest: io_uring_submit failed: %s\n", strerror(-ret));
    }
}

void TcpConnection::handleClose()
{
    // 保护 TcpConnection，防止在回调过程中被销毁
    std::shared_ptr<TcpConnection> guard(shared_from_this());
    if (closeCallback_)
    {
        closeCallback_(guard);
    }
}

void TcpConnection::connectEstablished()
{
    // 将状态设置为已连接
    setState(TcpConnectionState::kConnected);
    // 这里调用 connectionCallback_
    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed()
{
    if (state_ == TcpConnectionState::kConnected || state_ == TcpConnectionState::kDisconnecting)
    {
        setState(TcpConnectionState::kDisconnected);
    }
    // Socket 对象析构时会自动 close(fd)，
    // io_uring 中挂起的请求会因为 fd 关闭而以 -ECANCELED 或 -EBADF 失败。
}

// 协程模式下，这些回调如果不使用，可以留空。
// 提供实现以避免链接错误。
// void TcpConnection::handleRead(int) {}
// void TcpConnection::handleWrite(int) {}

// Awaitable 的实现 (解决 TcpConnection 不完整类型问题)
// #include "AsyncRead.hpp"
// #include "AsyncWrite.hpp"

// void AsyncReadAwaitable::await_suspend(std::coroutine_handle<> handle)
// {
//     conn_->getReadContext().coro_handle = handle;
//     conn_->submitReadRequest(nbytes_);
// }

// int AsyncReadAwaitable::await_resume() const noexcept
// {
//     int n = conn_->getReadContext().result_;
//     if (n > 0)
//     {
//         conn_->inputBuffer().hasWritten(n);
//     }
//     return n;
// }

// void AsyncWriteAwaitable::await_suspend(std::coroutine_handle<> handle)
// {
//     conn_->getWriteContext().coro_handle = handle;
//     conn_->submitWriteRequest();
// }

// int AsyncWriteAwaitable::await_resume() const noexcept
// {
//     int n = conn_->getWriteContext().result_;
//     if (n > 0)
//     {
//         conn_->outputBuffer().retrieve(n);
//     }
//     return n;
// }
