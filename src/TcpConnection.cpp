#include "TcpConnection.hpp"

TcpConnection::TcpConnection(EventLoop *loop, int sockfd, const InetAddress &peerAddr)
    : loop_(loop),
      socket_(sockfd),
      state_(0),
      reading_(false),
      inputBuffer_(),
      outputBuffer_(),
      localAddr_(socket_.getLocalAddress()),
      peerAddr_(peerAddr),
      connectionCallback_(nullptr),
      messageCallback_(nullptr),
      writeCompleteCallback_(nullptr),
      highWaterMarkCallback_(nullptr),
      closeCallback_(nullptr)
{
}

TcpConnection::~TcpConnection()
{
}