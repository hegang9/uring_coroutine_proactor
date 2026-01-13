## 介绍
该系统是一个基于 Proactor 模式和 Linux io_uring 异步 I/O 接口的高性能 C++ TCP 服务器框架。其核心功能是提供纯异步的网络 I/O 处理能力，区别于传统的 Reactor (epoll) 模型，该系统利用 io_uring 实现“内核执行 I/O，完成后通知应用”的机制。系统支持高并发连接处理，通过全异步的 Accept、Read、Write 操作减少系统调用开销，并结合批量提交（Batch Submission）和零拷贝特性，旨在实现极致的网络吞吐量和低延迟，适用于需要处理海量并发连接的高性能网络服务场景。

在架构设计上，系统采用了 One Ring Per Thread（即 Main-Sub Proactor）的多线程模型。系统由一个主线程（Main Proactor）和多个工作线程（Sub Proactor）组成。主线程持有一个独立的 io_uring 实例，专门负责监听端口和分发新建立的连接；工作线程池中的每个线程也各自持有一个独立的 io_uring 实例，负责接管已连接 Socket 的所有后续读写操作和业务逻辑处理。这种设计确保了每个连接的生命周期绑定在一个特定的线程中，实现了无锁编程，避免了多线程竞争，同时能够充分利用多核 CPU 资源。

核心控制层主要包含 TcpServer、EventLoop 和 Acceptor 三大模块。TcpServer 作为系统的总控入口，负责启动服务器、管理 Acceptor 和 EventLoopThreadPool。Acceptor 封装了监听 Socket，利用 IORING_OP_ACCEPT 异步接受连接。EventLoop 是系统的核心引擎，封装了 io_uring 的提交队列（SQ）和完成队列（CQ），驱动整个异步事件循环。为了解决跨线程任务分发问题，EventLoop 内部不仅集成了任务队列，还利用 eventfd 机制实现了线程唤醒功能，使得主线程可以高效地将新连接均匀分发给工作线程（Round-Robin 负载均衡），唤醒阻塞在 I/O 等待中的子线程。

在业务连接与数据处理层，系统划分为 TcpConnection、IoContext 及辅助组件。TcpConnection 负责管理单个 TCP 连接的完整生命周期，处理异步读写请求的提交。为了在 C 语言风格的 io_uring 接口和 C++ 对象之间建立联系，系统设计了 IoContext 模块，它封装了单次 I/O 操作的上下文（如文件描述符、缓冲区指针、回调函数等），作为 user_data 传递给内核，确保请求完成 review 时能准确回调。此外，系统还

包含 Buffer 模块用于应用层数据缓冲，Codec 模块处理 TCP 粘包/拆包问题，以及 MemoryPool 模块优化频繁的小块内存分配，共同保障数据传输的高效与稳定



## 构建命令
mkdir build
cd build
cmake ..
make -j"$(nproc)"

## 连接命令
nc 127.0.0.1 8888