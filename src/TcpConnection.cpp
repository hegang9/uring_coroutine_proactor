#include "TcpConnection.hpp"

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

void TcpConnection::handleRead()
{
}
