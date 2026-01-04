#include "TcpConnection.hpp"
#include <cstring>
#include <unistd.h>

TcpConnection::TcpConnection(EventLoop *loop, int sockfd, const InetAddress &peerAddr)
    : loop_(loop),
      socket_(sockfd),
      state_(0),
      reading_(false),
      inputBuffer_(),
      outputBuffer_(),
      readContext_(IoType::Read, sockfd),
      writeContext_(IoType::Write, sockfd),
      localAddr_(socket_.getLocalAddress()),
      peerAddr_(peerAddr),
      connectionCallback_(nullptr),
      messageCallback_(nullptr),
      writeCompleteCallback_(nullptr),
      highWaterMarkCallback_(nullptr),
      closeCallback_(nullptr)
{
    // 这里不用shared_from_this()，因为此时对象还未完全构造好，shared_from_this()会抛出异常
    readContext_.handler = std::bind(&TcpConnection::handleRead, this, std::placeholders::_1);
    writeContext_.handler = std::bind(&TcpConnection::handleWrite, this, std::placeholders::_1);
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
    if (state_.load() == TcpConnectionState::kConnected ||
        state_.load() == TcpConnectionState::kDisconnecting)
    {
        setState(TcpConnectionState::kDisconnecting);
        // 在所属的EventLoop线程中执行关闭操作，使用queueInLoop将关闭操作放入事件循环队列，以保证线程安全，否则可能会在错误的线程中关闭连接；使用shared_from_this确保TcpConnection对象在关闭过程中不会被销毁
        loop_->queueInLoop(std::bind(&TcpConnection::handleClose, shared_from_this()));
    }
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
