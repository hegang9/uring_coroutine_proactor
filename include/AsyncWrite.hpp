#pragma once
#include <coroutine>

class TcpConnection;

class AsyncWriteAwaitable
{
public:
    AsyncWriteAwaitable(TcpConnection *conn)
        : conn_(conn) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    int await_resume() const noexcept;
    ~AsyncWriteAwaitable() = default;

private:
    TcpConnection *conn_;
};