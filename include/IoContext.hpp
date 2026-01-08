#pragma once

#include <functional>
#include <coroutine>

#include "MemoryPool.hpp"

/**
 * IO 上下文，用于绑定到 io_uring 的 user_data
 * 作用：保存与某个 IO 操作相关的上下文信息，包括操作类型、文件描述符、缓冲区指针和完成回调函数
 */

// 前向声明
class TcpConnection;

// IO 操作类型
enum class IoType
{
    Read,
    Write,
    Accept,
    Connect,
    Timeout
};

// IO 上下文，用于绑定到 io_uring 的 user_data
struct IoContext
{
    IoType type;
    int fd; // 文件描述符

    // 回调函数，当 IO 完成时调用
    // 对于 Accept，参数通常是 (res, 0)
    // 对于 Read/Write，参数是 (bytes_transferred, 0)
    std::function<void(int)> handler;

    // 协程句柄 (用于协程模式)
    std::coroutine_handle<> coro_handle;

    int result_; // 暂存 IO 操作结果，用作将io_uring读写操作的结果中转到协程

    IoContext(IoType t, int f) : type(t), fd(f), coro_handle(nullptr), result_(0) {}

    // 禁用拷贝和赋值
    IoContext(const IoContext &) = delete;
    IoContext &operator=(const IoContext &) = delete;
};
