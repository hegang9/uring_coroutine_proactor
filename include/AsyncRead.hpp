#pragma once
#include <coroutine>

class TcpConnection;

class AsyncReadAwaitable
{
public:
    AsyncReadAwaitable(TcpConnection *conn, size_t nbytes)
        : conn_(conn), nbytes_(nbytes) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    int await_resume() const noexcept;
    ~AsyncReadAwaitable() = default;

private:
    TcpConnection *conn_;
    size_t nbytes_;
};