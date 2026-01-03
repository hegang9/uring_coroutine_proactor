#pragma once
#include <coroutine>
#include "TcpConnection.hpp"
#include "Buffer.hpp"

class AsyncWriteAwaitable
{
public:
    AsyncWriteAwaitable(TcpConnection *conn)
        : conn_(conn) {}
    bool awaite_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
    {
        // 将协程句柄保存到IoContext中，以便在写操作完成时恢复协程
        conn_->getWriteContext().coro_handle = handle;
        // 提交io_uring写请求
        conn_->submitWriteRequest();
        // 返回void表示挂起协程
    }
    int await_resume() const noexcept
    {
        // 返回实际写入的字节数
        return conn_->getWriteContext().result_;
    }
    ~AsyncWriteAwaitable() = default;

private:
    TcpConnection *conn_;
};