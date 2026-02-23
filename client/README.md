这个客户端包含了 5 种典型的测试场景，并添加了详细的中文注释：

正常 Echo 测试：发送一段数据，验证服务器是否能完整返回。
慢速客户端测试：模拟网络极差的情况，每次只发送 1 个字节，测试服务器的缓冲区处理和粘包/半包逻辑。
空闲连接测试：连接后什么都不做，睡眠 6 秒（根据你 ucp.conf 中配置的 read_timeout_ms = 5000），验证服务器是否能正确触发超时并踢出空闲连接。
异常断开测试：发送一半数据后，直接发送 RST 包（通过设置 SO_LINGER）强制关闭连接，测试服务器的异常捕获和资源回收。
并发连接风暴测试：瞬间发起 100 个并发连接然后迅速关闭，测试服务器的 Acceptor 和连接池抗压能力。

编译和运行指南
你可以直接在终端中运行以下命令来编译它：
```
cd /home/hegang/UCP
g++ client/test_client.cpp -o client/test_client -std=c++11 -pthread
```

运行所有测试（默认）：
./client/test_client
只运行某一个特定的测试（例如只测空闲连接超时）：
./client/test_client 3
查看帮助信息：
./client/test_client help
```

---

## 2. 长稳测试 (Long-term Stability Test)

长稳测试的目的是验证服务器在长时间、高并发的压力下，是否会出现内存泄漏、文件句柄 (FD) 泄漏或崩溃。

### 2.1 如何使用 `wrk` 跑 1~3 小时？

`wrk` 的 `-d` 参数支持指定时间单位（`s` 秒, `m` 分钟, `h` 小时）。

**跑 1 小时的命令示例：**
```bash
# -t 8: 8个线程
# -c 1000: 1000个并发连接
# -d 1h: 持续运行 1 小时
wrk -t8 -c1000 -d1h http://127.0.0.1:6666/
```
*(注意：如果你的服务器是纯 TCP Echo Server 而不是 HTTP Server，`wrk` 发送 HTTP 请求后，你的服务器会原样返回。这对于压测网络 I/O 也是完全可以的。)*

### 2.2 如何监控是否泄漏或崩溃？

在 `wrk` 运行的这 1~3 小时内，你需要新开一个终端，定期观察服务器进程的状态。

首先，找到你服务器进程的 PID：
```bash
pidof proactor_test
# 假设输出的 PID 是 12345
```

#### 监控指标 1：内存泄漏 (Memory Leak)
如果内存泄漏，进程占用的物理内存 (RES) 会随着时间不断上涨，永远不回落。

**方法 A：使用 `top` 实时观察**
```bash
top -p 12345
```
* **看 `RES` 列**：这是进程实际占用的物理内存。在压测刚开始时会涨，但稳定后应该保持在一个水平线上下波动。如果 1 小时内 `RES` 一直在稳步增加，说明有内存泄漏。

**方法 B：使用 `pmap` 查看内存映射详情**
```bash
pmap -x 12345 | tail -n 1
```
* 记录下 `RSS` 的总值，隔半小时看一次对比。

#### 监控指标 3：文件句柄泄漏 (FD Leak)
在 Linux 中，每个 Socket 连接都是一个文件句柄。如果连接断开后服务器没有正确 `close()`，句柄数会不断累加，最终导致 `Too many open files` 错误。

**方法 A：统计当前打开的句柄总数**
```bash
ls /proc/12345/fd | wc -l
```
* 在压测期间，这个数字应该接近你设置的并发数（例如 1000 + 几个日志/监听句柄）。
* **关键点**：当 `wrk` 压测**结束**后，再次运行这个命令。如果数字没有回落到个位数（或者初始状态），说明发生了句柄泄漏！

**方法 B：使用 `lsof` 查看具体是什么句柄没关**
```bash
lsof -p 12345
```

#### 监控指标 4：崩溃 (Crash)
* **最直观的**：看运行服务器的那个终端，进程是不是退出了，有没有打印 `Segmentation fault (core dumped)`。
* **看系统日志**：如果进程突然消失了，可以通过 `dmesg -T | tail -n 20` 查看内核日志，看是不是因为 OOM (Out of Memory) 被系统杀掉了，或者发生了段错误。

### 2.3 终极防泄漏工具：Valgrind (可选，但极度加分)

如果你想在简历上写“通过 Valgrind 验证无内存泄漏”，可以在**非压测**环境下（因为 Valgrind 会让程序变慢 10-50 倍），用 Valgrind 启动服务器，然后用 `test_client` 跑一遍所有测试，再正常关闭服务器。

```bash
# 安装 valgrind
sudo apt install valgrind

# 用 valgrind 启动服务器
valgrind --leak-check=full --show-leak-kinds=all ./bin/proactor_test config/ucp.conf
```
运行完客户端测试后，按 `Ctrl+C` 停止服务器，Valgrind 会打印一份详细的内存泄漏报告。如果最后显示 `definitely lost: 0 bytes`，那就完美了。