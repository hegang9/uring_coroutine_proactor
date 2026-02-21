#include "TcpConnection.hpp"

#include <unistd.h>

#include <cstring>

#include "Logger.hpp"

TcpConnection::TcpConnection(const std::string &name, EventLoop *loop, int sockfd, const InetAddress &peerAddr)
    : name_(name), loop_(loop), socket_(sockfd), state_(TcpConnectionState::kConnecting), reading_(false),
      curReadBuffer_(nullptr), curReadBufferSize_(0), curReadBufferOffset_(0), outputBuffer_(),
      readContext_(IoType::Read, sockfd), writeContext_(IoType::Write, sockfd),
      timeoutContext_(IoType::Timeout, sockfd), readTimeout_(0), readTimeoutSpec_(),
      localAddr_(socket_.getLocalAddress()), peerAddr_(peerAddr), connectionCallback_(nullptr), closeCallback_(nullptr)
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

void TcpConnection::reset()
{
    socket_.reset();
    state_.store(TcpConnectionState::kDisconnected);
    closeCallbackInvoked_.store(false);
    reading_ = false;
    outputBuffer_.reset();
    // 读写上下文不需要重置fd，因为TcpConnection对象销毁时，fd已经关闭
    readContext_.coro_handle = nullptr;
    readContext_.result_ = 0;
    if (readContext_.idx >= 0)
    {
        loop_->returnRegisteredBuffer(readContext_.idx);
    }
    readContext_.idx = -1; // 重置 registered buffer 索引
    curReadBuffer_ = nullptr;
    curReadBufferSize_ = 0;
    curReadBufferOffset_ = 0;
    writeContext_.coro_handle = nullptr;
    writeContext_.result_ = 0;
    loop_ = nullptr;
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
    if (!isConnected())
    {
        LOG_WARN("TcpConnection::submitReadRequest: state not connected, name={}", name_);
        return;
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        LOG_ERROR("TcpConnection::submitReadRequest: SQ full");
        return;
    }
    int idx = loop_->getRegisteredBufferIndex();
    if (idx >= 0)
    {
        // 使用已注册缓冲区进行读操作
        void *buf = loop_->getRegisteredBuffer(idx);
        // 把已注册缓冲区的索引存到 IoContext 的 idx 字段，以便完成后归还
        readContext_.idx = idx;
        io_uring_prep_read_fixed(sqe, socket_.getFd(), buf, nbytes, 0, idx);
        io_uring_sqe_set_data(sqe, &readContext_);
    }
    else
    {
        // 输出错误信息
        LOG_ERROR("TcpConnection::submitReadRequest: no registered buffer available");
    }

    if (readTimeout_ > std::chrono::milliseconds::zero())
    {
        io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);
        io_uring_sqe *ts_sqe = io_uring_get_sqe(&loop_->ring_);
        if (ts_sqe)
        {
            io_uring_prep_link_timeout(ts_sqe, &readTimeoutSpec_, 0);
            io_uring_sqe_set_data(ts_sqe, &timeoutContext_);
        }
        else
        {
            LOG_ERROR("TcpConnection::submitReadRequest: link timeout sqe unavailable");
        }
    }
}

void TcpConnection::submitReadRequestWithUserBuffer(char *userBuf, size_t userBufCap, size_t nbytes)
{
    if (!isConnected())
    {
        LOG_WARN("TcpConnection::submitReadRequestWithUserBuffer: state not connected, name={}", name_);
        return;
    }
    if (userBuf == nullptr || userBufCap == 0)
    {
        // 无效的用户缓冲区，直接返回
        LOG_ERROR("TcpConnection::submitReadRequestWithUserBuffer: invalid user buffer");
        return;
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        LOG_ERROR("TcpConnection::submitReadRequestWithUserBuffer: SQ full");
        return;
    }
    // 使用用户提供的缓冲区进行读操作
    io_uring_prep_read(sqe, socket_.getFd(), userBuf, std::min(userBufCap, nbytes), 0);
    io_uring_sqe_set_data(sqe, &readContext_);
    // 标记 idx 为 -1，表示未使用已注册缓冲区
    readContext_.idx = -1;

    if (readTimeout_ > std::chrono::milliseconds::zero())
    {
        io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);
        io_uring_sqe *ts_sqe = io_uring_get_sqe(&loop_->ring_);
        if (ts_sqe)
        {
            io_uring_prep_link_timeout(ts_sqe, &readTimeoutSpec_, 0);
            io_uring_sqe_set_data(ts_sqe, &timeoutContext_);
        }
        else
        {
            LOG_ERROR("TcpConnection::submitReadRequestWithUserBuffer: link timeout sqe unavailable");
        }
    }
}

void TcpConnection::submitWriteRequest()
{
    if (!isConnected() && !isDisconnecting())
    {
        LOG_WARN("TcpConnection::submitWriteRequest: invalid state, name={}", name_);
        return;
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        LOG_ERROR("TcpConnection::submitWriteRequest: SQ full");
        return;
    }

    // 准备写操作
    // 注意：write 操作不应该修改 outputBuffer_
    // 的可读位置，直到写操作完成(handleWrite)
    io_uring_prep_write(sqe, socket_.getFd(), outputBuffer_.readBeginAddr(), outputBuffer_.readableBytes(), 0);
    io_uring_sqe_set_data(sqe, &writeContext_);
    // 标记未使用已注册缓冲区
    writeContext_.idx = -1;
}

void TcpConnection::submitWriteRequestWithRegBuffer(void *buf, size_t len, int idx)
{
    if (!isConnected() && !isDisconnecting())
    {
        LOG_WARN("TcpConnection::submitWriteRequestWithRegBuffer: invalid state, name={}", name_);
        return;
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop_->ring_);
    if (!sqe)
    {
        LOG_ERROR("TcpConnection::submitWriteRequestWithRegBuffer: SQ full");
        return;
    }

    // 使用已注册缓冲区进行零拷贝写操作
    io_uring_prep_write_fixed(sqe, socket_.getFd(), buf, len, 0, idx);
    io_uring_sqe_set_data(sqe, &writeContext_);
    // 记录已注册缓冲区索引，写完后由调用者归还
    writeContext_.idx = idx;
}

void TcpConnection::setTimeout(std::chrono::milliseconds timeout)
{
    readTimeout_ = timeout;
    readTimeoutSpec_.tv_sec = timeout.count() / 1000;
    readTimeoutSpec_.tv_nsec = (timeout.count() % 1000) * 1000000;
}

void TcpConnection::handleClose()
{
    TcpConnectionState state = state_.load();
    if (state == TcpConnectionState::kDisconnected)
    {
        return;
    }
    state_.store(TcpConnectionState::kDisconnecting);
    if (closeCallbackInvoked_.exchange(true))
    {
        return;
    }
    // 保护 TcpConnection，防止在回调过程中被销毁
    std::shared_ptr<TcpConnection> guard(shared_from_this());
    if (closeCallback_)
    {
        closeCallback_(guard);
    }
}

void TcpConnection::releaseCurReadBuffer()
{
    if (readContext_.idx >= 0)
    {
        loop_->returnRegisteredBuffer(readContext_.idx);
        readContext_.idx = -1; // 重置 idx，防止重复归还
    }
    curReadBuffer_ = nullptr;
    curReadBufferSize_ = 0;
    curReadBufferOffset_ = 0;
}

void TcpConnection::connectEstablished()
{
    // 将状态设置为已连接
    setState(TcpConnectionState::kConnected);

    // 初始化 IoContext 的弱引用，用于 Cancel CQE 安全检查
    readContext_.connection = shared_from_this();
    writeContext_.connection = shared_from_this();
    timeoutContext_.connection = shared_from_this();
    timeoutContext_.handler = [self = shared_from_this()](int res) {
        if (res == -ECANCELED) // 返回-ECANCELED表示没有超时，直接返回（省略）
            return;
        if (!self->isConnected())
            return;
        self->forceClose(); // 说明发生超时，强制关闭连接
    };

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
    closeCallbackInvoked_.store(true);
    // Socket 对象析构时会自动 close(fd)，
    // io_uring 中挂起的请求会因为 fd 关闭而以 -ECANCELED 或 -EBADF 失败。

    // 这里只设置连接状态，是因为TcpConnection对象是使用shared_ptr管理的，当没有引用时会自动销毁
}

// 协程模式下，这些回调如果不使用，可以留空。
// 提供实现以避免链接错误。
// void TcpConnection::handleRead(int) {}
// void TcpConnection::handleWrite(int) {}