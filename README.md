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
*   **依赖库**: [liburing](https://github.com/axboe/liburing) (推荐 v2.2 及以上版本)和fmt库。
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
thread_num = 16
read_timeout_ms = 5000

[event_loop]
ring_entries = 32768
sqpoll = true
sqpoll_idle_ms = 50
registered_buffers_count = 16384
registered_buffer_size = 4096
pending_queue_capacity = 65536

[log]
level = INFO
file = logs/server.log
max_size = 104857600
max_files = 10
async = true
console = true
flush_interval_ms = 1000
```


## 日志系统
基于 `fmt` 库实现的高性能异步日志系统，核心特性：
- **异步无阻塞**：I/O 线程写入无锁队列，后台线程负责落盘
- **分级输出**：TRACE/DEBUG/INFO/WARN/ERROR/FATAL，运行时可配置
- **自动滚动**：按大小或时间滚动日志文件
- **多 Sink 支持**：同时输出到控制台和文件

使用示例：
```cpp
LOG_INFO("Server started on {}:{}", ip, port);
LOG_WARN("Connection timeout: fd={}", fd);
LOG_ERROR("Accept failed: {}", strerror(errno));
```

配置项（`config/ucp.conf` 的 `[log]` 段）：
- `level`：日志级别（TRACE/DEBUG/INFO/WARN/ERROR/FATAL）
- `file`：日志文件路径
- `max_size`：单个日志文件最大大小（字节）
- `max_files`：保留的日志文件数量
- `async`：是否启用异步日志（true/false）
- `console`：是否输出到控制台（true/false）
- `flush_interval_ms`：后台线程刷新间隔（毫秒）




## 构建命令
mkdir build
cd build
cmake ..
make -j"$(nproc)"

## 启动命令
cd /home/hegang/UCP && sudo sh -c "ulimit -n 100000 && ulimit -l unlimited && ./bin/proactor_test config/ucp.conf"

## 内存泄漏检测启动命令 (使用 Valgrind)
cd /home/hegang/UCP && sudo sh -c "ulimit -n 100000 && ulimit -l unlimited && valgrind --leak-check=full --show-leak-kinds=all ./bin/proactor_test config/ucp.conf"

## 依次执行正常收发、慢速发送、空闲超时、异常断开和少量并发的测试客户端程序
cd /home/hegang/UCP
./client/test_client all

## 压测脚本
./benchmark.sh

## 性能测试数据
在Intel Core i7-14700HX CPU，16个子工作线程，pingpong业务逻辑条件下，使用wrk进行压力测试数据如下：
```text
Connections: 3000
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.93ms    1.83ms  29.57ms   85.21%
    Req/Sec    55.07k    26.01k  158.73k    63.89%
  Latency Distribution
     50%    1.17ms
     75%    2.36ms
     90%    4.58ms
     99%    8.36ms
  21975865 requests in 19.33s, 2.23GB read
  Socket errors: connect 906, read 0, write 0, timeout 1826
Requests/sec: 1137024.45
Transfer/sec:    118.19MB
==========================================================
Connections: 3100
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.94ms    1.79ms  28.07ms   85.31%
    Req/Sec    55.16k    27.17k  151.30k    56.14%
  Latency Distribution
     50%    1.21ms
     75%    2.29ms
     90%    4.54ms
     99%    8.28ms
  21922823 requests in 20.09s, 2.23GB read
  Socket errors: connect 961, read 0, write 0, timeout 0
Requests/sec: 1091131.82
Transfer/sec:    113.42MB
==========================================================
Connections: 3200
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.52ms    1.58ms  39.24ms   88.04%
    Req/Sec    59.24k    35.03k  137.25k    53.40%
  Latency Distribution
     50%    0.98ms
     75%    1.46ms
     90%    3.59ms
     99%    7.83ms
  23564018 requests in 20.10s, 2.39GB read
  Socket errors: connect 1366, read 0, write 0, timeout 0
Requests/sec: 1172534.16
Transfer/sec:    121.89MB
==========================================================
Connections: 3300
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3300 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.57ms    1.59ms  27.64ms   87.11%
    Req/Sec    57.66k    32.87k  147.78k    57.29%
  Latency Distribution
     50%    0.99ms
     75%    1.58ms
     90%    3.77ms
     99%    7.75ms
  22978590 requests in 19.20s, 2.33GB read
  Socket errors: connect 1449, read 0, write 0, timeout 1520
Requests/sec: 1196849.19
Transfer/sec:    124.41MB
==========================================================
Connections: 3400
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.75ms    1.80ms  61.81ms   85.58%
    Req/Sec    55.30k    27.58k  137.42k    65.67%
  Latency Distribution
     50%    1.03ms
     75%    1.96ms
     90%    4.31ms
     99%    8.05ms
  22010875 requests in 19.12s, 2.23GB read
  Socket errors: connect 1447, read 0, write 0, timeout 1629
Requests/sec: 1151013.45
Transfer/sec:    119.65MB
==========================================================
Connections: 3500
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.51ms    9.50ms 213.23ms   99.23%
    Req/Sec    55.84k    30.61k  126.41k    60.82%
  Latency Distribution
     50%    1.17ms
     75%    2.09ms
     90%    4.59ms
     99%   10.27ms
  22119217 requests in 19.10s, 2.25GB read
  Socket errors: connect 1348, read 0, write 0, timeout 2068
Requests/sec: 1158362.75
Transfer/sec:    120.41MB
==========================================================
Connections: 3600
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3600 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.90ms    1.78ms  25.12ms   85.35%
    Req/Sec    55.60k    28.94k  141.35k    64.91%
  Latency Distribution
     50%    1.19ms
     75%    2.13ms
     90%    4.51ms
     99%    8.29ms
  22042657 requests in 20.10s, 2.24GB read
  Socket errors: connect 1422, read 0, write 0, timeout 0
Requests/sec: 1096652.98
Transfer/sec:    114.00MB
==========================================================
Connections: 3700
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3700 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.81ms    1.75ms  26.81ms   86.27%
    Req/Sec    56.76k    32.54k  151.85k    58.92%
  Latency Distribution
     50%    1.15ms
     75%    1.90ms
     90%    4.32ms
     99%    8.36ms
  22582506 requests in 19.12s, 2.29GB read
  Socket errors: connect 1572, read 0, write 0, timeout 1990
Requests/sec: 1180823.03
Transfer/sec:    122.75MB
==========================================================
Connections: 3800
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3800 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.75ms    1.74ms  35.99ms   86.57%
    Req/Sec    54.77k    32.84k  141.30k    60.87%
  Latency Distribution
     50%    1.11ms
     75%    1.80ms
     90%    4.22ms
     99%    8.33ms
  21727679 requests in 19.14s, 2.21GB read
  Socket errors: connect 1790, read 0, write 0, timeout 1920
Requests/sec: 1134934.46
Transfer/sec:    117.98MB
==========================================================
Connections: 3900
Running 20s test @ http://192.168.2.69:6666
  20 threads and 3900 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.49ms    1.48ms  26.11ms   88.83%
    Req/Sec    60.03k    37.79k  156.42k    62.21%
  Latency Distribution
     50%    1.03ms
     75%    1.48ms
     90%    3.32ms
     99%    7.52ms
  23854640 requests in 19.12s, 2.42GB read
  Socket errors: connect 1946, read 0, write 0, timeout 1822
Requests/sec: 1247384.53
Transfer/sec:    129.67MB
==========================================================
Connections: 4000
Running 20s test @ http://192.168.2.69:6666
  20 threads and 4000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.62ms    1.55ms  28.08ms   87.31%
    Req/Sec    60.58k    33.17k  166.98k    68.93%
  Latency Distribution
     50%    1.09ms
     75%    1.65ms
     90%    3.81ms
     99%    7.66ms
  23976229 requests in 20.10s, 2.43GB read
  Socket errors: connect 1915, read 0, write 0, timeout 0
Requests/sec: 1193004.26
Transfer/sec:    124.01MB
==========================================================
```