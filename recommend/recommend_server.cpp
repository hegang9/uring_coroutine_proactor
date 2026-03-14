/**
 * @file recommend_server.cpp
 * @brief 推荐服务主程序
 *
 * 使用UCP框架（基于io_uring + C++20协程）构建一个高性能推荐服务。
 *
 * 核心特性：
 *   - 异步I/O：基于io_uring固定缓冲区的网络通信
 *   - 协程处理：用C++20协程简洁表达异步流程
 *   - 高并发：支持万级并发连接
 *   - 低延迟：毫秒级推荐响应
 *
 * 请求流程：
 *   HTTP POST /recommend -> 异步读取请求体 -> 解析JSON -> 调用处理器 -> 构建JSON响应 -> 异步发送
 *
 * 搜广推系统的关键指标：
 *   - P99延迟：通常需要<100ms（包括所有网络往返）
 *   - 吞吐量：单机可能处理5000+ QPS
 *   - 缓存命中率：直接影响延迟（特征缓存命中率应>80%）
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
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

#include "FeatureStore.hpp"
#include "RankingModel.hpp"
#include "RecommendProtocol.hpp"
#include "RecommendationHandler.hpp"

// ===================== 全局服务实例 =====================

// 特征存储服务（带LRU缓存）
std::unique_ptr<FeatureStore> g_featureStore;
// 推荐处理器
std::unique_ptr<RecommendationHandler> g_recommendationHandler;

// ===================== 工具函数 =====================

/**
 * @brief 生成简单的追踪ID
 *
 * 不依赖libuuid库，使用随机数生成伪UUID格式的ID
 */
static std::string generateTraceId()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 8; i++)
    {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++)
    {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++)
    {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++)
    {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++)
    {
        ss << std::hex << dis(gen);
    }

    return ss.str();
}

// ===================== HTTP请求解析 =====================

/**
 * @brief 简单的HTTP请求解析器
 *
 * 解析HTTP请求头和请求体（JSON格式）
 */
struct HttpRequest
{
    std::string_view method;
    std::string_view path;
    std::string_view body;
    size_t contentLength = 0;
    bool keepAlive = true;
    bool complete = false;

    /**
     * @brief 从缓冲区解析HTTP请求
     *
     * @param data 请求数据指针
     * @param len 数据长度
     * @return 已消费的字节数，0表示请求不完整
     */
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

        // 提取 method (GET/POST等)
        size_t methodEnd = requestLine.find(' ');
        if (methodEnd == std::string_view::npos)
            return 0;
        method = requestLine.substr(0, methodEnd);

        // 提取 path (/recommend等)
        size_t pathStart = methodEnd + 1;
        size_t pathEnd = requestLine.find(' ', pathStart);
        if (pathEnd == std::string_view::npos)
            return 0;
        path = requestLine.substr(pathStart, pathEnd - pathStart);

        // 解析 Content-Length 请求头
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

// ===================== HTTP响应构建 =====================

/**
 * @brief 构建HTTP响应
 *
 * @param body 响应体内容
 * @param contentType MIME类型（如application/json）
 * @param keepAlive 是否keep-alive
 * @return HTTP响应字符串
 */
std::string buildHttpResponse(std::string_view body, const std::string &contentType = "application/json",
                              bool keepAlive = true)
{
    std::string response;
    response.reserve(512 + body.size());

    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: ";
    response += std::to_string(body.size());
    response += "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    response += "\r\n";
    response += body;

    return response;
}

/**
 * @brief 构建错误响应
 */
std::string buildErrorResponse(const std::string &error, bool keepAlive = true)
{
    std::string body = R"({"error":")" + error + R"("})";
    return buildHttpResponse(body, "application/json", keepAlive);
}

// ===================== 推荐服务协程处理器 =====================

/**
 * @brief 推荐服务的协程处理函数
 *
 * 对于每个TCP连接，运行一个协程来处理其上的所有HTTP请求。
 * 支持keep-alive，一个连接可以处理多个请求。
 *
 * 协程的优势：
 *   - 不需要复杂的状态机
 *   - 代码流程与同步逻辑相似
 *   - 异步I/O操作用co_await表达
 *   - 底层自动转换为状态机（编译器生成）
 *
 * @param conn TCP连接对象
 */
Task recommendationServiceTask(std::shared_ptr<TcpConnection> conn)
{
    try
    {
        std::string buffer; // 累积接收的数据

        while (true)
        {
            // ============ 1. 异步读取数据 ============
            // co_await会触发异步读操作，协程在I/O完成后自动恢复
            int n = co_await conn->asyncRead(4096);

            if (n <= 0)
            {
                LOG_INFO("Client closed connection: fd={}", conn->getName());
                break;
            }

            LOG_TRACE("Received {} bytes from {}", n, conn->getName());

            // ============ 2. 获取接收到的数据 ============
            auto [dataPtr, dataLen] = conn->getDataFromBuffer();
            buffer.append(dataPtr, dataLen);

            // 释放读缓冲区（重要！允许继续读取）
            conn->releaseCurReadBuffer();

            // ============ 3. 解析HTTP请求 ============
            // 处理可能的多个请求（keep-alive）
            while (!buffer.empty())
            {
                HttpRequest req;
                size_t consumed = req.parse(buffer.data(), buffer.size());

                if (consumed == 0)
                {
                    // 请求数据不完整，继续等待更多数据
                    break;
                }

                // ============ 4. 处理推荐请求 ============
                std::string responseBody;

                if (req.path == "/recommend" && req.method == "POST")
                {
                    // 解析JSON请求体
                    RecommendRequest request;
                    if (!request.parseFromJson(req.body))
                    {
                        LOG_WARN("Failed to parse recommend request: {}", req.body);
                        responseBody = buildErrorResponse("Invalid request format", req.keepAlive);
                    }
                    else
                    {
                        // 生成追踪ID（用于日志关联）
                        request.traceId = generateTraceId();

                        // 调用推荐处理器
                        LOG_DEBUG("Handling recommendation request: user_id={}, count={}, scene={}, trace_id={}",
                                  request.userId, request.count, request.scene, request.traceId);

                        auto startTime = std::chrono::high_resolution_clock::now();
                        RecommendResponse response = g_recommendationHandler->handleRecommendation(request);
                        auto endTime = std::chrono::high_resolution_clock::now();

                        int handlerLatencyUs =
                            std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

                        // 记录性能指标
                        LOG_INFO("Recommendation completed: user_id={}, recall_count={}, final_count={}, "
                                 "total_latency_us={}, handler_latency_us={}",
                                 request.userId, response.recallCandidateCount, response.finalCount,
                                 response.totalLatencyUs, handlerLatencyUs);

                        responseBody = buildHttpResponse(response.toJson(), "application/json", req.keepAlive);
                    }
                }
                else if (req.path == "/health")
                {
                    // 健康检查接口
                    responseBody = buildHttpResponse(R"({"status":"ok"})", "application/json", req.keepAlive);
                }
                else if (req.path == "/stats")
                {
                    // 统计信息接口
                    auto stats = g_featureStore->getCacheStats();
                    std::string statsJson = R"({"feature_cache":{"user_hit_rate":)" +
                                            std::to_string(stats.userHitRate) + R"(,"item_hit_rate":)" +
                                            std::to_string(stats.itemHitRate) + R"(}})";
                    responseBody = buildHttpResponse(statsJson, "application/json", req.keepAlive);
                }
                else
                {
                    // 404 Not Found
                    responseBody = buildErrorResponse("Not Found", req.keepAlive);
                }

                // ============ 5. 异步发送响应 ============
                int written = co_await conn->asyncSend(responseBody);
                if (written < 0)
                {
                    LOG_ERROR("Failed to send response: fd={}, written={}", conn->getName(), written);
                    co_return;
                }

                LOG_TRACE("Sent {} bytes to {}", written, conn->getName());

                // ============ 6. 移除已处理的请求 ============
                buffer.erase(0, consumed);

                // ============ 7. 检查是否需要关闭连接 ============
                if (!req.keepAlive)
                {
                    LOG_DEBUG("Client requested close: {}", conn->getName());
                    conn->forceClose();
                    co_return;
                }
                // 否则继续处理下一个请求
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Service task error: {}, exception: {}", conn->getName(), e.what());
    }

    LOG_DEBUG("Closing connection: {}", conn->getName());
    conn->forceClose();
}

// ===================== main函数 =====================

int main(int argc, char **argv)
{
    // 配置文件路径
    std::string configPath = "config/ucp.conf";
    if (argc > 1 && argv[1] != nullptr)
    {
        configPath = argv[1];
    }

    // ==================== 初始化日志系统 ====================
    Logger::Options logOptions;
    Logger::init(logOptions);

    // 加载配置文件
    Config config;
    std::string configError;
    if (!config.loadFromFile(configPath, &configError))
    {
        LOG_ERROR("Config load failed: {}", configError);
        Logger::shutdown();
        return 1;
    }

    // 根据配置设置日志级别
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

    logOptions.logFile = config.getString("log.file", "logs/recommend_server.log");
    logOptions.maxFileSize = config.getSizeT("log.max_size", 100 * 1024 * 1024);
    logOptions.maxFiles = config.getSizeT("log.max_files", 10);
    logOptions.async = config.getBool("log.async", true);
    logOptions.console = config.getBool("log.console", true);
    logOptions.flushInterval = config.getDurationMs("log.flush_interval_ms", std::chrono::milliseconds(1000));

    Logger::shutdown();
    Logger::init(logOptions);
    LOG_INFO("Logger initialized: level={}, file={}", logLevelStr, logOptions.logFile);

    // ==================== 初始化内存池 ====================
    LOG_DEBUG("Initializing memory pool...");
    HashBucket::initMemoryPool();
    LOG_DEBUG("Memory pool initialized successfully.");

    // ==================== 初始化EventLoop ====================
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

    // 初始化注册缓冲区池
    loop.initRegisteredBuffers();
    LOG_DEBUG("Registered buffers initialized.");

    // ==================== 初始化推荐服务 ====================
    LOG_INFO("Initializing recommendation service...");

    // 创建特征存储服务（带LRU缓存）
    int userCacheSize = config.getInt("recommend.user_cache_size", 10000);
    int itemCacheSize = config.getInt("recommend.item_cache_size", 100000);
    g_featureStore = std::make_unique<FeatureStore>(userCacheSize, itemCacheSize);
    LOG_INFO("FeatureStore initialized: user_cache_size={}, item_cache_size={}", userCacheSize, itemCacheSize);

    // 创建推荐处理器
    g_recommendationHandler = std::make_unique<RecommendationHandler>(g_featureStore.get());
    LOG_INFO("RecommendationHandler initialized.");

    // ==================== 创建TcpServer ====================
    std::string listenIp = config.getString("server.ip", "0.0.0.0");
    int listenPort = config.getInt("server.port", 8888);
    InetAddress listenAddr(static_cast<uint16_t>(listenPort), listenIp);

    LOG_DEBUG("Creating TcpServer...");
    std::string serverName = config.getString("server.name", "RecommendationService");
    TcpServer server(&loop, listenAddr, serverName);
    LOG_DEBUG("TcpServer created.");

    // ==================== 设置连接回调 ====================
    LOG_DEBUG("Setting connection callback...");
    server.setConnectionCallback([](const std::shared_ptr<TcpConnection> &conn) {
        LOG_INFO("New connection established: {} -> {}", conn->getPeerAddr().toIpPort(),
                 conn->getLocalAddr().toIpPort());
        // 启动推荐服务协程
        recommendationServiceTask(conn);
    });
    LOG_DEBUG("Connection callback set.");

    // ==================== 启动服务器 ====================
    LOG_DEBUG("Starting server...");
    int threadNum = config.getInt("server.thread_num", 16);
    server.setThreadNum(threadNum);
    server.setEventLoopOptions(loopOptions);
    server.setReadTimeout(config.getDurationMs("server.read_timeout_ms", std::chrono::milliseconds(5000)));

    server.start();
    LOG_INFO("RecommendationService started on {}:{} with {} worker threads.", listenIp, listenPort, threadNum);
    LOG_INFO("Endpoints:");
    LOG_INFO("  POST /recommend      - Get recommendations");
    LOG_INFO("  GET  /health         - Health check");
    LOG_INFO("  GET  /stats          - Performance stats");

    // ==================== 进入事件循环 ====================
    LOG_DEBUG("Entering event loop...");
    loop.loop();
    LOG_INFO("Event loop exited.");

    // ==================== 清理资源 ====================
    g_recommendationHandler.reset();
    g_featureStore.reset();
    Logger::shutdown();

    return 0;
}
