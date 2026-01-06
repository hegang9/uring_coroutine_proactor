#include "AsyncWrite.hpp"
#include "TcpConnection.hpp"
#include "Buffer.hpp"

void AsyncWriteAwaitable::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // 将协程句柄保存到IoContext中，以便在写操作完成时恢复协程
    conn_->getWriteContext().coro_handle = handle;
    // 提交io_uring写请求
    conn_->submitWriteRequest();
    // 返回void表示挂起协程
}

int AsyncWriteAwaitable::await_resume() const noexcept
{
    // 返回实际写入的字节数，在后续开发中根据此结果进行错误处理
    int n = conn_->getWriteContext().result_;
    if (n > 0)
    {
        conn_->getOutputBuffer().retrieve(n); // 从输出缓冲区移除已写入的数据
    }
    return n;
}