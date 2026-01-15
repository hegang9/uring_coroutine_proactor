#include "TcpConnection.hpp"

#include <unistd.h>

#include <cstring>

TcpConnection::TcpConnection(const std::string& name, EventLoop* loop,
                             int sockfd, const InetAddress& peerAddr)
    : name_(name),
      loop_(loop),
      socket_(sockfd),
      state_(TcpConnectionState::kDisconnected),
      reading_(false),
      curReadBuffer_(nullptr),
      curReadBufferSize_(0),
      curReadBufferOffset_(0),
      outputBuffer_(),
      readContext_(IoType::Read, sockfd),
      writeContext_(IoType::Write, sockfd),
      localAddr_(socket_.getLocalAddress()),
      peerAddr_(peerAddr),
      connectionCallback_(nullptr),
      closeCallback_(nullptr) {
  // 协程模式下，不需要绑定传统的回调函数 (handleRead/handleWrite)
}

TcpConnection::~TcpConnection() {
  // 逻辑关闭连接，调用socket的析构函数释放资源
}

void TcpConnection::setState(TcpConnectionState state) { state_.store(state); }

void TcpConnection::reset() {
  socket_.reset();
  state_.store(TcpConnectionState::kDisconnected);
  reading_ = false;
  outputBuffer_.reset();
  // 读写上下文不需要重置fd，因为TcpConnection对象销毁时，fd已经关闭
  readContext_.coro_handle = nullptr;
  readContext_.result_ = 0;
  if (readContext_.idx >= 0) {
    loop_->returnRegisteredBuffer(readContext_.idx);
  }
  readContext_.idx = -1;  // 重置 registered buffer 索引
  curReadBuffer_ = nullptr;
  curReadBufferSize_ = 0;
  curReadBufferOffset_ = 0;
  writeContext_.coro_handle = nullptr;
  writeContext_.result_ = 0;
  loop_ = nullptr;
}

void TcpConnection::shutdown() {
  // 发送FIN包，半关闭写端
  if (state_.load() == TcpConnectionState::kConnected) {
    setState(TcpConnectionState::kDisconnecting);
    socket_.shutdownWrite();
  }
}

void TcpConnection::forceClose() {
  TcpConnectionState expected = TcpConnectionState::kConnected;
  if (state_.compare_exchange_strong(expected,
                                     TcpConnectionState::kDisconnecting)) {
    loop_->queueInLoop(
        std::bind(&TcpConnection::handleClose, shared_from_this()));
  }
  // 如果已经是 kDisconnecting，不重复提交
}

void TcpConnection::submitReadRequest(size_t nbytes) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&loop_->ring_);
  if (!sqe) {
    // 极其罕见的情况：SQ 满了。
    // 实际生产中可能需要处理，这里简单打印
    fprintf(stderr, "TcpConnection::submitReadRequest: SQ full\n");
    return;
  }
  int idx = loop_->getRegisteredBufferIndex();
  if (idx >= 0) {
    // 使用已注册缓冲区进行读操作
    void* buf = loop_->getRegisteredBuffer(idx);
    // 把已注册缓冲区的索引存到 IoContext 的 idx 字段，以便完成后归还
    readContext_.idx = idx;
    io_uring_prep_read_fixed(sqe, socket_.getFd(), buf, nbytes, 0, idx);
    io_uring_sqe_set_data(sqe, &readContext_);

  } else {
    // TODO:扩容固定缓冲区
  }
}

void TcpConnection::submitReadRequestWithUserBuffer(char* userBuf,
                                                    size_t userBufCap,
                                                    size_t nbytes) {
  if (userBuf == nullptr || userBufCap == 0) {
    // 无效的用户缓冲区，直接返回
    fprintf(stderr,
            "TcpConnection::submitReadRequestWithUserBuffer: invalid user "
            "buffer\n");
    return;
  }
  struct io_uring_sqe* sqe = io_uring_get_sqe(&loop_->ring_);
  if (!sqe) {
    // 极其罕见的情况：SQ 满了。
    // 实际生产中可能需要处理，这里简单打印
    fprintf(stderr,
            "TcpConnection::submitReadRequestWithUserBuffer: SQ full\n");
    return;
  }
  // 使用用户提供的缓冲区进行读操作
  io_uring_prep_read(sqe, socket_.getFd(), userBuf,
                     std::min(userBufCap, nbytes), 0);
  io_uring_sqe_set_data(sqe, &readContext_);
  // 标记 idx 为 -1，表示未使用已注册缓冲区
  readContext_.idx = -1;
}

void TcpConnection::submitWriteRequest() {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&loop_->ring_);
  if (!sqe) {
    // 极其罕见的情况：SQ 满了。
    // 实际生产中可能需要处理，这里简单打印
    fprintf(stderr, "TcpConnection::submitWriteRequest: SQ full\n");
    return;
  }

  // 准备写操作
  // 注意：write 操作不应该修改 outputBuffer_
  // 的可读位置，直到写操作完成(handleWrite)
  io_uring_prep_write(sqe, socket_.getFd(), outputBuffer_.readBeginAddr(),
                      outputBuffer_.readableBytes(), 0);
  io_uring_sqe_set_data(sqe, &writeContext_);

  // 注意：写操作不使用已注册缓冲区，因为数据在 outputBuffer_
  // 中，且可能不是页对齐的
}

void TcpConnection::handleClose() {
  // 保护 TcpConnection，防止在回调过程中被销毁
  std::shared_ptr<TcpConnection> guard(shared_from_this());
  if (closeCallback_) {
    closeCallback_(guard);
  }
}

void TcpConnection::releaseCurReadBuffer() {
  if (readContext_.idx >= 0) {
    loop_->returnRegisteredBuffer(readContext_.idx);
    readContext_.idx = -1;  // 重置 idx，防止重复归还
  }
  curReadBuffer_ = nullptr;
  curReadBufferSize_ = 0;
  curReadBufferOffset_ = 0;
}

void TcpConnection::connectEstablished() {
  // 将状态设置为已连接
  setState(TcpConnectionState::kConnected);
  // 这里调用 connectionCallback_
  if (connectionCallback_) {
    connectionCallback_(shared_from_this());
  }
}

void TcpConnection::connectDestroyed() {
  if (state_ == TcpConnectionState::kConnected ||
      state_ == TcpConnectionState::kDisconnecting) {
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
