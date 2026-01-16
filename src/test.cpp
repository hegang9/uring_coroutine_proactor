#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "CoroutineTask.hpp"
#include "EventLoop.hpp"
#include "InetAddress.hpp"
#include "MemoryPool.hpp"
#include "TcpConnection.hpp"
#include "TcpServer.hpp"

// 协程业务逻辑：Echo 服务
// 这是一个协程函数，因为它返回 Task 并且使用了 co_await
// Tcp粘包和拆包问题应该在应用层进行处理
Task echoTask(std::shared_ptr<TcpConnection> conn) {
  try {
    while (true) {
      // 1. 异步读取数据 (挂起，直到数据到来)
      // asyncRead 返回 AsyncReadAwaitable，co_await 会触发 submitReadRequest
      // 这里的 1024 是期望读取的最大字节数
      int n = co_await conn->asyncRead(1024);
      // 可指定用户缓冲区版本：
      // char userBuf[2048];
      // int n = co_await conn->asyncRead(userBuf, sizeof(userBuf), 1024);

      if (n <= 0) {
        // n == 0: 对端关闭连接 (FIN)
        // n < 0:  发生错误
        break;
      }

      // 2. 获取数据
      auto [dataPtr, dataLen] = conn->getDataFromBuffer();

      // 3. 异步发送数据 (挂起，直到数据写完)
      // 零拷贝模式：直接从已注册缓冲区发送，不经过 outputBuffer_
      int written = co_await conn->asyncSendZeroCopy();
      // 发送完成后释放已注册缓冲区
      conn->releaseCurReadBuffer();

      if (written < 0) {
        break;
      }
    }
  } catch (const std::exception& e) {
    std::cout << "[Coroutine] Error: " << e.what() << std::endl;
  }

  conn->forceClose();
}

int main() {
  // 0. 初始化内存池（必须在使用任何内存池分配前调用）
  std::cout << "[DEBUG] Initializing memory pool..." << std::endl;
  HashBucket::initMemoryPool();
  std::cout << "[DEBUG] Memory pool initialized successfully." << std::endl;

  // 1. 初始化 EventLoop
  std::cout << "[DEBUG] Creating EventLoop..." << std::endl;
  EventLoop loop;
  std::cout << "[DEBUG] EventLoop created." << std::endl;

  // 1.5 初始化注册缓冲区池
  loop.initRegisteredBuffers();

  // 2. 设置监听地址
  std::cout << "[DEBUG] Creating InetAddress..." << std::endl;
  InetAddress listenAddr(8888);
  std::cout << "[DEBUG] InetAddress created." << std::endl;

  // 3. 创建 TcpServer
  std::cout << "[DEBUG] Creating TcpServer..." << std::endl;
  TcpServer server(&loop, listenAddr);
  std::cout << "[DEBUG] TcpServer created." << std::endl;

  // 4. 设置新连接回调
  // 当有新连接时，TcpServer 会调用这个 lambda
  // 我们在这里启动协程来处理这个连接
  std::cout << "[DEBUG] Setting connection callback..." << std::endl;
  server.setConnectionCallback([](const std::shared_ptr<TcpConnection>& conn) {
    // 启动协程 (Fire and Forget)
    // echoTask 返回一个 Task 对象，该对象在这一行结束时析构。
    // 但由于 Task::promise_type::final_suspend 返回 std::suspend_never，
    // 且协程句柄被保存在 IoContext 中（通过 await_suspend），
    // 所以协程会继续存活，直到函数体执行完毕。
    echoTask(conn);
  });
  std::cout << "[DEBUG] Connection callback set." << std::endl;

  // 5. 启动服务器
  std::cout << "[DEBUG] Setting thread num..." << std::endl;
  // SQPOLL 模式下，每个 Loop 会额外占用一个内核 Polling 线程
  // 所以 Worker 线程数建议设置为物理核心数的一半，以避免 CPU 竞争
  server.setThreadNum(8);
  std::cout << "[DEBUG] Thread num set. Starting server..." << std::endl;
  server.start();
  std::cout << "[DEBUG] Server started." << std::endl;

  std::cout << "Server started on port 8888. Press Ctrl+C to stop."
            << std::endl;

  // 6. 进入事件循环
  std::cout << "[DEBUG] Entering event loop..." << std::endl;
  loop.loop();
  std::cout << "[DEBUG] Event loop exited." << std::endl;

  return 0;
}