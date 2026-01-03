#pragma once
#include <coroutine>
#include "TcpConnection.hpp"
#include "Buffer.hpp"

class AsyncReadAwaitable
{
public:
    AsyncReadAwaitable(TcpConnection *conn, size_t nbytes)
        : conn_(conn), nbytes_(nbytes) {}
    bool awaite_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
    {
        // 将协程句柄保存到IoContext中，以便在读操作完成时恢复协程
        conn_->getReadContext().coro_handle = handle;
        // 提交io_uring读请求
        conn_->submitReadRequest(nbytes_);
        // 返回void表示挂起协程
    }
    int await_resume() const noexcept
    {
        // 返回实际读取的字节数
        return conn_->getReadContext().result_;
    }
    ~AsyncReadAwaitable() = default;

private:
    TcpConnection *conn_;
    size_t nbytes_;
};