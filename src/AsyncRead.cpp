#include "AsyncRead.hpp"
#include "TcpConnection.hpp"
#include "Buffer.hpp"

void AsyncReadAwaitable::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // 将协程句柄保存到IoContext中，以便在读操作完成时恢复协程
    conn_->getReadContext().coro_handle = handle;
    // 提交io_uring读请求
    conn_->submitReadRequest(nbytes_);
    // 返回void表示挂起协程
}

int AsyncReadAwaitable::await_resume() const noexcept
{
    int n = conn_->getReadContext().result_;
    conn_->getInputBuffer().hasWritten(n); // 更新输入缓冲区的写索引
    // 返回实际读取的字节数，在后续开发中根据此结果进行错误处理
    return n;
}