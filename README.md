## UCP: 基于 io_uring 的高性能 C++ Proactor 网络框架
### 项目简介
该系统是一个专为 Linux 环境设计的高性能 TCP 服务器框架。它摒弃了传统的 epoll + Reactor 模型，全面拥抱 Linux io_uring 异步 I/O 接口，实现了纯粹的 Proactor 模式。该系统的核心目标是极致的性能与低延迟。通过“内核执行 I/O，完成后通知应用”的机制，结合 C++20 协程、无锁任务队列、零拷贝等现代技术，最大化利用多核 CPU 资源，适用于海量并发连接的高频交易、即时通讯及高性能网关等场景。

### 核心架构设计
在架构设计上，系统采用了 One Ring Per Thread（即 Main-Sub Proactor）的多线程模型。系统由一个主线程（Main Proactor）和多个工作线程（Sub Proactor）组成。主线程持有一个独立的 io_uring 实例，专门负责监听端口和分发新建立的连接；工作线程池中的每个线程也各自持有一个独立的 io_uring 实例，负责接管已连接 Socket 的所有后续读写操作和业务逻辑处理。这种设计确保了每个连接的生命周期绑定在一个特定的线程中，实现了无锁编程，避免了多线程竞争，同时能够充分利用多核 CPU 资源。同时，为了解决异步编程中“**回调地狱**”的问题，系统引入了 C++20 无栈协程。通过定制 promise_type 和 awaiter，将 io_uring 的异步完成事件与协程的 resume 动作无缝对接。

核心控制层主要包含 TcpServer、EventLoop 和 Acceptor 三大模块。TcpServer 作为系统的总控入口，负责启动服务器、管理 Acceptor 和 EventLoopThreadPool。Acceptor 封装了监听 Socket，利用 IORING_OP_ACCEPT 异步接受连接。EventLoop 是系统的核心引擎，封装了 io_uring 的提交队列（SQ）和完成队列（CQ），驱动整个异步事件循环。为了解决跨线程任务分发问题，EventLoop 内部不仅集成了任务队列，还利用 **eventfd 机制**实现了线程唤醒功能，使得主线程可以高效地将新连接均匀分发给工作线程（Round-Robin 负载均衡），唤醒阻塞在 I/O 等待中的子线程。

在业务连接与数据处理层，系统划分为 TcpConnection、IoContext 及辅助组件。TcpConnection 负责管理单个 TCP 连接的完整生命周期，处理异步读写请求的提交。为了在 C 语言风格的 io_uring 接口和 C++ 对象之间建立联系，系统设计了 IoContext 模块，它封装了单次 I/O 操作的上下文（如文件描述符、缓冲区指针、回调函数等），作为 user_data 传递给内核，确保请求完成 review 时能准确回调。此外，系统还包含 Buffer 模块用于应用层数据缓冲，以及 MemoryPool 模块优化频繁的小块内存分配，共同保障数据传输的高效与稳定。

### 性能优化设计
1. 使用内存池优化小块内存分配；
2. 配置 IORING_SETUP_SQPOLL 开启内核轮询线程。应用提交任务仅需更新队列尾指针，无需任何系统调用 (Zero Syscall)，实现低延迟；
3. 实现多个连接读写请求的批量提交，大大减少了系统调用次数；
4. 引入了C++20无栈协程，避免了“回调地狱”，提升了系统的可维护性和可读性。C++20 协程在编译器层面进行了深度优化，其性能表现相较于传统回调模式相差无几甚至更好；
5. 利用了 io_uring 的 注册缓冲区 (Registered Buffer) 特性，减少 I/O 操作期间内核进行内存页面映射的开销，从而实现从内核视角的零拷贝（避免了每次 I/O 都调用的 get_user_pages/put_page 开销），大幅提升高并发下的读写性能；
6. 利用了 io_uring 提供的一种为任何异步请求设置超时的Link Timeout机制，避免了用户态手动开启定时器、取消定时器的复杂竞态条件，不需要用户态维护繁重的红黑树或时间轮（Time Wheel），而是利用内核的高效定时机制；
7. 自实现无锁队列作为任务队列，避免多个线程竞争锁资源导致性能下降，实现了吞吐量跨越式提升。

## 环境依赖 (Prerequisites)
运行本系统需要较新的 Linux 内核以支持 io_uring 的完整特性，同时需要支持 C++20 的编译器。
*   **操作系统**: Linux Kernel **5.10+** (推荐 5.19 或更高版本以获得最佳性能及完善的 io_uring 支持)。
*   **编译器**: GCC **11.0+** 或 Clang **14.0+** (必须完整支持 C++20 标准)。
*   **依赖库**: [liburing](https://github.com/axboe/liburing) (推荐 v2.2 及以上版本)。
*   **构建工具**: CMake **3.10+**。

## 项目目录结构
```text
├── src/            # 核心源文件
├── include/        # 头文件
├── memory/         # 内存池实现
└── bin/            # 编译输出目录
```

## 配置系统
项目新增轻量配置系统，使用 `ini` 风格配置文件（支持 `[section]` 与 `key=value`）。默认配置文件路径为 `config/ucp.conf`，也可通过启动参数指定。

示例：
```text
[server]
name = TcpServer
ip = 0.0.0.0
port = 8888
thread_num = 8
read_timeout_ms = 5000

[event_loop]
ring_entries = 32768
sqpoll = true
sqpoll_idle_ms = 50
registered_buffers_count = 16384
registered_buffer_size = 4096
pending_queue_capacity = 65536
```

启动方式：
```text
./proactor_test config/ucp.conf
```

## 性能测试数据
在Intel Core i7-14700HX CPU，8个子工作线程，Echo回显业务逻辑条件下，使用wrk进行压力测试数据如下：
```text
Connections: 1500
Running 20s test @ http://192.168.1.105:8888
  20 threads and 1500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.38ms  345.01us  27.33ms   85.76%
    Req/Sec    55.78k    35.27k  542.69k    99.17%
  Latency Distribution
     50%    1.33ms
     75%    1.45ms
     90%    1.74ms
     99%    2.34ms
  21363511 requests in 19.30s, 2.17GB read
  Socket errors: connect 0, read 0, write 0, timeout 1482
Requests/sec: 1106660.90
Transfer/sec:    115.04MB

Connections: 3000
Running 20s test @ http://192.168.1.105:8888
  20 threads and 3000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.91ms  703.17us  37.56ms   83.34%
    Req/Sec    51.07k     5.12k  216.59k    96.35%
  Latency Distribution
     50%    2.81ms
     75%    3.09ms
     90%    3.68ms
     99%    5.14ms
  20333657 requests in 20.10s, 2.06GB read
Requests/sec: 1011601.88
Transfer/sec:    105.16MB

Connections: 4500
Running 20s test @ http://192.168.1.105:8888
  20 threads and 4500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.45ms    0.88ms  74.85ms   84.07%
    Req/Sec    46.38k    15.58k  157.83k    54.82%
  Latency Distribution
     50%    3.30ms
     75%    3.64ms
     90%    4.51ms
     99%    6.05ms
  18434841 requests in 20.10s, 1.87GB read
  Socket errors: connect 1283, read 0, write 0, timeout 0
Requests/sec: 917160.69
Transfer/sec:     95.34MB

Connections: 6000
Running 20s test @ http://192.168.1.105:8888
  20 threads and 6000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.96ms  687.60us  36.22ms   80.82%
    Req/Sec    51.81k    23.90k  167.36k    62.34%
  Latency Distribution
     50%    2.85ms
     75%    3.17ms
     90%    3.76ms
     99%    5.20ms
  20530228 requests in 20.10s, 2.08GB read
  Socket errors: connect 2897, read 0, write 0, timeout 0
Requests/sec: 1021369.16
Transfer/sec:    106.17MB

Connections: 7500
Running 20s test @ http://192.168.1.105:8888
  20 threads and 7500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.01ms  461.37us   9.47ms   78.45%
    Req/Sec    55.08k    35.48k  160.83k    58.30%
  Latency Distribution
     50%    1.95ms
     75%    2.19ms
     90%    2.56ms
     99%    3.48ms
  21827616 requests in 20.10s, 2.22GB read
  Socket errors: connect 5196, read 0, write 0, timeout 0
Requests/sec: 1086124.07
Transfer/sec:    112.90MB

Connections: 9000
Running 20s test @ http://192.168.1.105:8888
  20 threads and 9000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.76ms  641.01us  16.44ms   79.91%
    Req/Sec    52.79k    28.56k  156.92k    72.06%
  Latency Distribution
     50%    2.67ms
     75%    2.98ms
     90%    3.53ms
     99%    4.79ms
  20823952 requests in 20.10s, 2.11GB read
  Socket errors: connect 6012, read 0, write 0, timeout 0
Requests/sec: 1036019.11
Transfer/sec:    107.69MB
```

## 构建命令
mkdir build
cd build
cmake ..
make -j"$(nproc)"

# 启动命令
cd /home/hegang/UCP/bin && sudo sh -c "ulimit -n 100000 && ulimit -l unlimited && ./proactor_test"

# 压测脚本
./benchmark.sh