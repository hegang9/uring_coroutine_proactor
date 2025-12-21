#pragma once

#include <functional>

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
    int fd;       // eventfd
    void *buffer; // 可选，用于读写操作

    // 回调函数，当 IO 完成时调用
    // 对于 Accept，参数通常是 (res, 0)
    // 对于 Read/Write，参数是 (bytes_transferred, 0)
    std::function<void(int)> handler;

    IoContext(IoType t, int f) : type(t), fd(f), buffer(nullptr) {}
};
