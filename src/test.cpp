#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include "TcpServer.hpp"
#include "EventLoop.hpp"
#include "TcpConnection.hpp"
#include "CoroutineTask.hpp"
#include "InetAddress.hpp"

// 协程业务逻辑：Echo 服务
// 这是一个协程函数，因为它返回 Task 并且使用了 co_await
Task echoTask(std::shared_ptr<TcpConnection> conn)
{
    std::string peerIp = conn->getPeerAddr().toIpPort();
    std::cout << "[Coroutine] New connection from " << peerIp << std::endl;

    try
    {
        while (true)
        {
            // 1. 异步读取数据 (挂起，直到数据到来)
            // asyncRead 返回 AsyncReadAwaitable，co_await 会触发 submitReadRequest
            // 这里的 1024 是期望读取的最大字节数
            int n = co_await conn->asyncRead(1024);

            if (n <= 0)
            {
                // n == 0: 对端关闭连接 (FIN)
                // n < 0:  发生错误
                std::cout << "[Coroutine] Connection closed by peer or error. res=" << n << " (" << peerIp << ")" << std::endl;
                break;
            }

            // 2. 获取数据
            // 注意：你的 AsyncReadAwaitable::await_resume 已经调用了 inputBuffer_.hasWritten(n)
            // 所以此时 inputBuffer_ 中已经有 n 字节的可读数据了。

            // 我们把数据读出来打印一下
            // 注意：readAllAsString 会清空 inputBuffer_，所以如果我们要 Echo 回去，
            // 需要先把数据拿出来，再塞到 outputBuffer_ 里。
            // 或者更高效的做法是直接操作 buffer，但为了演示清晰，我们先转成 string。
            std::string data = conn->getInputBuffer().readAllAsString();

            // 去掉末尾可能的换行符以便打印整洁
            std::string displayData = data;
            if (!displayData.empty() && displayData.back() == '\n')
                displayData.pop_back();
            std::cout << "[Coroutine] Received " << n << " bytes from " << peerIp << ": " << displayData << std::endl;

            // 3. 准备发送数据 (Echo)
            // 将收到的数据追加到输出缓冲区
            // conn->getOutputBuffer().append(data);

            // 4. 异步发送数据 (挂起，直到数据写完)
            // asyncWrite 返回 AsyncWriteAwaitable，co_await 会触发 submitWriteRequest
            // int written = co_await conn->asyncWrite();
            int written = co_await conn->asyncSend(data);

            if (written < 0)
            {
                std::cerr << "[Coroutine] Write error: " << written << std::endl;
                break;
            }
            // std::cout << "[Coroutine] Echoed " << written << " bytes back." << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Coroutine] Exception caught: " << e.what() << std::endl;
    }

    std::cout << "[Coroutine] Connection handler finishing for " << peerIp << std::endl;

    // 协程结束，强制关闭连接
    // 注意：conn 是 shared_ptr，只要协程栈帧还在，引用计数就在。
    // 协程结束后，conn 引用计数减一，如果归零则析构 TcpConnection。
    conn->forceClose();
}

int main()
{
    // 1. 初始化 EventLoop
    EventLoop loop;

    // 2. 设置监听地址
    InetAddress listenAddr(8888);

    // 3. 创建 TcpServer
    TcpServer server(&loop, listenAddr);

    // 4. 设置新连接回调
    // 当有新连接时，TcpServer 会调用这个 lambda
    // 我们在这里启动协程来处理这个连接
    server.setConnectionCallback([](const std::shared_ptr<TcpConnection> &conn)
                                 {
        // 启动协程 (Fire and Forget)
        // echoTask 返回一个 Task 对象，该对象在这一行结束时析构。
        // 但由于 Task::promise_type::final_suspend 返回 std::suspend_never，
        // 且协程句柄被保存在 IoContext 中（通过 await_suspend），
        // 所以协程会继续存活，直到函数体执行完毕。
        echoTask(conn); });

    // 5. 启动服务器
    std::cout << "Server started on port 8888. Press Ctrl+C to stop." << std::endl;
    server.start();

    // 6. 进入事件循环
    loop.loop();

    return 0;
}