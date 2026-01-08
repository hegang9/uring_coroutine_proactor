#pragma once
#include <coroutine>
#include <cstddef>
#include "MemoryPool.hpp"

class TcpConnection;

class AsyncWriteAwaitable
{
public:
    // 禁用拷贝和赋值
    AsyncWriteAwaitable(const AsyncWriteAwaitable &) = delete;
    AsyncWriteAwaitable &operator=(const AsyncWriteAwaitable &) = delete;
    AsyncWriteAwaitable(TcpConnection *conn)
        : conn_(conn) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    int await_resume() const noexcept;
    ~AsyncWriteAwaitable() = default;

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
};