#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "Config.hpp"
#include "CoroutineTask.hpp"
#include "EventLoop.hpp"
#include "InetAddress.hpp"
#include "Logger.hpp"
#include "MemoryPool.hpp"
#include "TcpConnection.hpp"
#include "TcpServer.hpp"

// ===================== 简单的 HTTP 解析器 =====================
// 解析 HTTP 请求，提取请求体（如果有）
struct HttpRequest
{
    std::string_view method;
    std::string_view path;
    std::string_view body;
    size_t contentLength = 0;
    bool keepAlive = true;
    bool complete = false;

    // 解析 HTTP 请求
    // 返回已消费的字节数，如果请求不完整返回 0
    size_t parse(const char *data, size_t len)
    {
        std::string_view sv(data, len);

        // 查找请求头结束位置 "\r\n\r\n"
        size_t headerEnd = sv.find("\r\n\r\n");
        if (headerEnd == std::string_view::npos)
        {
            return 0; // 请求头不完整
        }

        // 解析请求行
        size_t firstLineEnd = sv.find("\r\n");
        std::string_view requestLine = sv.substr(0, firstLineEnd);

        // 提取 method
        size_t methodEnd = requestLine.find(' ');
        if (methodEnd == std::string_view::npos)
            return 0;
        method = requestLine.substr(0, methodEnd);

        // 提取 path
        size_t pathStart = methodEnd + 1;
        size_t pathEnd = requestLine.find(' ', pathStart);
        if (pathEnd == std::string_view::npos)
            return 0;
        path = requestLine.substr(pathStart, pathEnd - pathStart);

        // 解析 Content-Length
        contentLength = 0;
        size_t clPos = sv.find("Content-Length:");
        if (clPos == std::string_view::npos)
        {
            clPos = sv.find("content-length:");
        }
        if (clPos != std::string_view::npos && clPos < headerEnd)
        {
            size_t valueStart = clPos + 15; // "Content-Length:" 长度
            while (valueStart < headerEnd && sv[valueStart] == ' ')
                valueStart++;
            size_t valueEnd = sv.find("\r\n", valueStart);
            if (valueEnd != std::string_view::npos)
            {
                std::string_view clValue = sv.substr(valueStart, valueEnd - valueStart);
                contentLength = 0;
                for (char c : clValue)
                {
                    if (c >= '0' && c <= '9')
                    {
                        contentLength = contentLength * 10 + (c - '0');
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }

        // 检查 Connection: keep-alive / close
        keepAlive = true;
        size_t connPos = sv.find("Connection:");
        if (connPos == std::string_view::npos)
        {
            connPos = sv.find("connection:");
        }
        if (connPos != std::string_view::npos && connPos < headerEnd)
        {
            size_t valueStart = connPos + 11;
            size_t valueEnd = sv.find("\r\n", valueStart);
            if (valueEnd != std::string_view::npos)
            {
                std::string_view connValue = sv.substr(valueStart, valueEnd - valueStart);
                if (connValue.find("close") != std::string_view::npos)
                {
                    keepAlive = false;
                }
            }
        }

        // 计算请求总长度
        size_t totalLen = headerEnd + 4 + contentLength;
        if (len < totalLen)
        {
            return 0; // 请求体不完整
        }

        // 提取请求体
        if (contentLength > 0)
        {
            body = sv.substr(headerEnd + 4, contentLength);
        }
        else
        {
            body = std::string_view();
        }

        complete = true;
        return totalLen;
    }
};

// 构建 HTTP 响应
std::string buildHttpResponse(std::string_view body, bool keepAlive = true)
{
    std::string response;
    response.reserve(256 + body.size());

    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: ";
    response += std::to_string(body.size());
    response += "\r\n";
    response += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    response += "\r\n";
    response += body;

    return response;
}

// ===================== HTTP Ping-Pong 协程任务 =====================
// 适合 wrk 等 HTTP 压测工具
Task httpPingPongTask(std::shared_ptr<TcpConnection> conn)
{
    try
    {
        std::string buffer; // 用于累积不完整的请求

        while (true)
        {
            // 1. 异步读取数据
            int n = co_await conn->asyncRead(4096);

            if (n <= 0)
            {
                LOG_DEBUG("Connection closed or error: fd={}, n={}", conn->getName(), n);
                break;
            }

            LOG_TRACE("Read {} bytes from {}", n, conn->getName());

            // 2. 获取数据并追加到缓冲区
            auto [dataPtr, dataLen] = conn->getDataFromBuffer();
            buffer.append(dataPtr, dataLen);

            // 释放读缓冲区
            conn->releaseCurReadBuffer();

            // 3. 尝试解析完整的 HTTP 请求
            while (!buffer.empty())
            {
                HttpRequest req;
                size_t consumed = req.parse(buffer.data(), buffer.size());

                if (consumed == 0)
                {
                    // 请求不完整，等待更多数据
                    break;
                }

                // 4. 构建 HTTP 响应（Echo: 将请求体原样返回）
                // 如果没有请求体，返回 "Hello from Proactor!"
                std::string_view responseBody = req.body.empty() ? std::string_view("Hello from Proactor!") : req.body;
                std::string response = buildHttpResponse(responseBody, req.keepAlive);

                // 5. 发送响应
                int written = co_await conn->asyncSend(response);
                if (written < 0)
                {
                    LOG_ERROR("Failed to send response: fd={}, written={}", conn->getName(), written);
                    co_return;
                }

                LOG_TRACE("Sent {} bytes to {}", written, conn->getName());

                // 6. 移除已处理的请求数据
                buffer.erase(0, consumed);

                // 7. 如果客户端请求关闭连接，则退出
                if (!req.keepAlive)
                {
                    LOG_DEBUG("Client requested close: {}", conn->getName());
                    conn->forceClose();
                    co_return;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("HTTP task error: {}, exception: {}", conn->getName(), e.what());
    }

    LOG_DEBUG("Closing connection: {}", conn->getName());
    conn->forceClose();
}

int main(int argc, char **argv)
{
    std::string configPath = "config/ucp.conf";
    if (argc > 1 && argv[1] != nullptr)
    {
        configPath = argv[1];
    }

    Config config;
    std::string configError;
    if (!config.loadFromFile(configPath, &configError))
    {
        std::cerr << "[ERROR] " << configError << std::endl;
        return 1;
    }

    // 0.5 初始化日志系统
    Logger::Options logOptions;
    std::string logLevelStr = config.getString("log.level", "INFO");
    if (logLevelStr == "TRACE")
        logOptions.level = LogLevel::TRACE;
    else if (logLevelStr == "DEBUG")
        logOptions.level = LogLevel::DEBUG;
    else if (logLevelStr == "INFO")
        logOptions.level = LogLevel::INFO;
    else if (logLevelStr == "WARN")
        logOptions.level = LogLevel::WARN;
    else if (logLevelStr == "ERROR")
        logOptions.level = LogLevel::ERROR;
    else if (logLevelStr == "FATAL")
        logOptions.level = LogLevel::FATAL;

    logOptions.logFile = config.getString("log.file", "logs/server.log");
    logOptions.maxFileSize = config.getSizeT("log.max_size", 100 * 1024 * 1024);
    logOptions.maxFiles = config.getSizeT("log.max_files", 10);
    logOptions.async = config.getBool("log.async", true);
    logOptions.console = config.getBool("log.console", true);
    logOptions.flushInterval = config.getDurationMs("log.flush_interval_ms", std::chrono::milliseconds(1000));

    Logger::init(logOptions);
    LOG_INFO("Logger initialized: level={}, file={}", logLevelStr, logOptions.logFile);

    // 0. 初始化内存池（必须在使用任何内存池分配前调用）
    LOG_DEBUG("Initializing memory pool...");
    HashBucket::initMemoryPool();
    LOG_DEBUG("Memory pool initialized successfully.");

    // 1. 初始化 EventLoop
    LOG_DEBUG("Creating EventLoop...");
    EventLoop::Options loopOptions;
    loopOptions.ringEntries = config.getSizeT("event_loop.ring_entries", loopOptions.ringEntries);
    loopOptions.sqpoll = config.getBool("event_loop.sqpoll", loopOptions.sqpoll);
    loopOptions.sqpollIdleMs =
        static_cast<unsigned int>(config.getSizeT("event_loop.sqpoll_idle_ms", loopOptions.sqpollIdleMs));
    loopOptions.registeredBuffersCount =
        config.getSizeT("event_loop.registered_buffers_count", loopOptions.registeredBuffersCount);
    loopOptions.registeredBuffersSize =
        config.getSizeT("event_loop.registered_buffer_size", loopOptions.registeredBuffersSize);
    loopOptions.pendingQueueCapacity =
        config.getSizeT("event_loop.pending_queue_capacity", loopOptions.pendingQueueCapacity);

    EventLoop loop(loopOptions);
    LOG_DEBUG("EventLoop created.");

    // 1.5 初始化注册缓冲区池
    loop.initRegisteredBuffers();
    LOG_DEBUG("Registered buffers initialized.");

    // 2. 设置监听地址
    LOG_DEBUG("Creating InetAddress...");
    std::string listenIp = config.getString("server.ip", "0.0.0.0");
    int listenPort = config.getInt("server.port", 8888);
    InetAddress listenAddr(static_cast<uint16_t>(listenPort), listenIp);
    LOG_INFO("Server will listen on {}:{}", listenIp, listenPort);

    // 3. 创建 TcpServer
    LOG_DEBUG("Creating TcpServer...");
    std::string serverName = config.getString("server.name", "TcpServer");
    TcpServer server(&loop, listenAddr, serverName);
    LOG_DEBUG("TcpServer created.");
    // 4. 设置新连接回调
    // 当有新连接时，TcpServer 会调用这个 lambda
    // 我们在这里启动协程来处理这个连接
    LOG_DEBUG("Setting connection callback...");
    server.setConnectionCallback([](const std::shared_ptr<TcpConnection> &conn) {
        LOG_INFO("New connection established: {} -> {}", conn->getPeerAddr().toIpPort(),
                 conn->getLocalAddr().toIpPort());
        // 启动 HTTP Ping-Pong 协程
        // 使用 httpPingPongTask 处理 HTTP 请求
        httpPingPongTask(conn);
    });
    LOG_DEBUG("Connection callback set.");

    // 5. 启动服务器
    LOG_DEBUG("Setting thread num...");
    // SQPOLL 模式下，每个 Loop 会额外占用一个内核 Polling 线程
    // 所以 Worker 线程数建议设置为物理核心数的一半，以避免 CPU 竞争
    int threadNum = config.getInt("server.thread_num", 8);
    server.setThreadNum(threadNum);
    server.setEventLoopOptions(loopOptions);
    server.setReadTimeout(config.getDurationMs("server.read_timeout_ms", std::chrono::milliseconds(5000)));
    LOG_DEBUG("Thread num set to {}. Starting server...", threadNum);
    server.start();
    LOG_INFO("Server started successfully with {} worker threads.", threadNum);

    std::cout << "Server started on port " << listenPort << ". Press Ctrl+C to stop." << std::endl;

    // 6. 进入事件循环
    LOG_DEBUG("Entering event loop...");
    loop.loop();
    LOG_INFO("Event loop exited.");

    // 7. 关闭日志系统
    Logger::shutdown();

    return 0;
}