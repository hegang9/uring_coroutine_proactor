### 1. Proactor (io_uring) vs Reactor (Muduo) 模块设计异同

虽然两者都采用了 **One Loop Per Thread** 的并发模型，但在核心模块的职责和交互上有显著差异。

| 模块              | Muduo (Reactor/epoll)                                                   | Proactor (io_uring)                                                                         | 核心差异点                                                          |
| :---------------- | :---------------------------------------------------------------------- | :------------------------------------------------------------------------------------------ | :------------------------------------------------------------------ |
| **EventLoop**     | 核心是 `epoll_wait`。负责监听 fd 的**可读/可写状态**。                  | 核心是 `io_uring_wait_cqe`。负责获取**异步操作的完成结果**。                                | **状态 vs 结果**。Reactor 告诉你“能读了”，Proactor 告诉你“读完了”。 |
| **Channel**       | 封装 fd 和感兴趣的事件 (EPOLLIN/OUT)。是 Loop 和 fd 的桥梁。            | **不需要 Channel**。通常直接将 `TcpConnection` 或 `Context` 指针绑定到 SQE 的 `user_data`。 | Proactor 不需要维护“关注列表”，每次操作都是一次性的提交。           |
| **TcpConnection** | `handleRead` 中调用 `read()` 系统调用。`handleWrite` 中调用 `write()`。 | `asyncRead` 提交 `IORING_OP_READ`。`asyncWrite` 提交 `IORING_OP_WRITE`。                    | **同步调用 vs 异步提交**。Proactor 的读写不阻塞，也不立即发生。     |
| **Buffer**        | 读：从内核 socket 读到 Buffer。写：从 Buffer 拷到内核 socket。          | **读：必须预先提供 Buffer 给内核**。写：提交 Buffer 指针给内核。                            | **Buffer 生命周期管理**。Proactor 必须保证 I/O 完成前 Buffer 有效。 |
| **Acceptor**      | 监听 listenfd 的可读事件，调用 `accept()`。                             | 提交 `IORING_OP_ACCEPT` 请求。                                                              | 同样是异步的。                                                      |

---

### 2. 主从线程分发实现方案 (Main Reactor -> Sub Reactor)

你想实现的是经典的 **Main-Sub Reactor** 模型（在 Proactor 语境下叫 **Main-Sub Proactor**）。

**目标**：
1.  **主线程 (Main Loop)**：只负责 `Accept` 新连接。
2.  **从属线程 (Sub Loop)**：负责处理已连接 Socket 的读写。
3.  **分发**：主线程拿到新连接 `fd` 后，把它“扔”给某个子线程。

#### 核心难点：跨线程任务分发
在 `io_uring` 中，子线程通常阻塞在 `io_uring_wait_cqe` 等待 I/O 完成。主线程不能直接操作子线程的 `io_uring` 实例（虽然 `io_uring` 是线程安全的，但多线程竞争同一个 Ring 性能极差，且逻辑复杂）。

**最佳实践方案：使用 `eventfd` 唤醒子线程**

#### 实现步骤

1.  **EventLoop 增强**：
    每个 `EventLoop` 内部持有一个 `eventfd`。
    *   初始化时，提交一个针对 `eventfd` 的 `IORING_OP_READ` 请求。
    *   当 `eventfd` 可读时，说明有主线程派发的任务来了。

2.  **任务队列**：
    每个 `EventLoop` 维护一个线程安全的任务队列 `std::vector<Functor> pendingFunctors_`。

3.  **分发逻辑 (TcpServer::newConnection)**：
    *   主线程 `Acceptor` 获得新连接 `client_fd`。
    *   主线程选择一个子线程 `io_loop` (Round-Robin)。
    *   主线程调用 `io_loop->runInLoop(cb)`。
    *   `cb` 是一个 Lambda，内容是：`new TcpConnection(io_loop, client_fd, ...)`。

4.  **唤醒逻辑 (EventLoop::runInLoop)**：
    *   将 `cb` 放入子线程的 `pendingFunctors_`。
    *   往子线程的 `eventfd` 写入 8 字节数据。
    *   子线程的 `io_uring_wait_cqe` 返回（因为 `eventfd` 读完成了）。
    *   子线程处理完 CQE 后，检查 `pendingFunctors_` 并执行 `cb`。
    *   `cb` 执行时，创建 `TcpConnection`，并立即提交第一个 `IORING_OP_READ` 请求。

#### 代码示意

**EventLoop.hpp (伪代码)**
```cpp
class EventLoop {
public:
    void runInLoop(Functor cb) {
        if (isInLoopThread()) {
            cb();
        } else {
            queueInLoop(cb);
        }
    }

    void queueInLoop(Functor cb) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingFunctors_.push_back(cb);
        }
        wakeup(); // 写 eventfd
    }

    void wakeup() {
        uint64_t one = 1;
        write(wakeupFd_, &one, sizeof(one)); // 触发 io_uring 返回
    }
    
    // ... loop() 中处理完 CQE 后，执行 doPendingFunctors()
};
```

**TcpServer.cpp (分发)**
```cpp
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
    EventLoop* ioLoop = threadPool_->getNextLoop();
    
    // 将创建连接的任务派发给子线程
    ioLoop->runInLoop([=](){
        // 此时已在子线程中运行
        TcpConnection::Ptr conn(new TcpConnection(ioLoop, sockfd, ...));
        connections_[connName] = conn;
        conn->connectEstablished(); // 提交第一个 asyncRead
    });
}
```

这种设计保证了 **One Loop Per Thread** 的原则：每个 `io_uring` 实例只被一个线程操作，避免了锁竞争，性能最高。