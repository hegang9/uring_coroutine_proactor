#pragma once
#include <coroutine>
#include <cstddef>
#include "MemoryPool.hpp"

class TcpConnection;

class AsyncReadAwaitable
{
public:
    // 禁用拷贝和赋值
    AsyncReadAwaitable(const AsyncReadAwaitable &) = delete;
    AsyncReadAwaitable &operator=(const AsyncReadAwaitable &) = delete;
    AsyncReadAwaitable(TcpConnection *conn, std::size_t nbytes)
        : conn_(conn), nbytes_(nbytes) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    int await_resume() const noexcept;
    ~AsyncReadAwaitable() = default;

    // 重载new/delete，接入内存池
    static void *operator new(size_t size)
    {
        return HashBucket::useMemory(size);
    }
    static void operator delete(void *p, size_t size)
    {
        HashBucket::freeMemory(p, size);
    }

private:
    TcpConnection *conn_;
    std::size_t nbytes_;
};