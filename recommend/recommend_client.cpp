/**
 * @file recommend_client.cpp
 * @brief 推荐服务测试客户端
 *
 * 用于测试推荐服务的客户端程序。
 * 支持：
 *   - 发送推荐请求
 *   - 并发测试
 *   - 性能统计
 *   - 健康检查
 */

#include <arpa/inet.h>
#include <chrono>
#include <climits>
#include <cstring>
#include <inttypes.h>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// ===================== 常量定义 =====================

const char *DEFAULT_SERVER = "127.0.0.1";
const int DEFAULT_PORT = 8888;
const int DEFAULT_REQUESTS = 100;
const int DEFAULT_THREADS = 1;

// ===================== 统计信息 =====================

/**
 * @brief 性能统计结构
 */
struct Stats
{
    int totalRequests = 0;
    int successfulRequests = 0;
    int failedRequests = 0;
    int64_t totalLatencyUs = 0;
    int64_t minLatencyUs = INT64_MAX;
    int64_t maxLatencyUs = 0;

    void reset()
    {
        totalRequests = 0;
        successfulRequests = 0;
        failedRequests = 0;
        totalLatencyUs = 0;
        minLatencyUs = INT64_MAX;
        maxLatencyUs = 0;
    }

    void print(const char *title = "Statistics")
    {
        printf("\n========== %s ==========\n", title);
        printf("Total Requests:     %d\n", totalRequests);
        printf("Successful:         %d\n", successfulRequests);
        printf("Failed:             %d\n", failedRequests);
        if (successfulRequests > 0 && totalLatencyUs > 0)
        {
            printf("Average Latency:    %.2f us\n", (double)totalLatencyUs / successfulRequests);
            printf("Min Latency:        %" PRId64 " us\n", minLatencyUs);
            printf("Max Latency:        %" PRId64 " us\n", maxLatencyUs);
            printf("Throughput:         %.2f req/s\n", (double)successfulRequests * 1000000 / totalLatencyUs);
        }
        printf("==============================\n\n");
    }
};

// 全局统计信息
Stats g_stats;
pthread_mutex_t g_statsMutex = PTHREAD_MUTEX_INITIALIZER;

// ===================== HTTP客户端实现 =====================

/**
 * @brief 创建TCP连接
 */
int connectToServer(const char *server, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(server);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

/**
 * @brief 发送HTTP请求并接收响应
 */
bool sendRecommendRequest(int sock, uint64_t userId, int count, const std::string &scene, int64_t &latencyUs)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    // 构建JSON请求体
    std::string requestBody = "{\"user_id\":" + std::to_string(userId) + ",\"count\":" + std::to_string(count) +
                              ",\"scene\":\"" + scene + "\"}";

    // 构建HTTP请求
    std::string httpRequest = "POST /recommend HTTP/1.1\r\n";
    httpRequest += "Host: localhost:8888\r\n";
    httpRequest += "Content-Type: application/json\r\n";
    httpRequest += "Content-Length: " + std::to_string(requestBody.size()) + "\r\n";
    httpRequest += "Connection: keep-alive\r\n";
    httpRequest += "\r\n";
    httpRequest += requestBody;

    // 发送请求
    if (send(sock, httpRequest.c_str(), httpRequest.size(), 0) < 0)
    {
        perror("send");
        return false;
    }

    // 接收响应
    char responseBuffer[8192];
    int recvLen = recv(sock, responseBuffer, sizeof(responseBuffer) - 1, 0);
    if (recvLen <= 0)
    {
        perror("recv");
        return false;
    }

    responseBuffer[recvLen] = '\0';

    // 简单验证响应：检查是否包含"items"字段
    if (strstr(responseBuffer, "\"items\"") == nullptr)
    {
        fprintf(stderr, "Invalid response: %s\n", responseBuffer);
        return false;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

    return true;
}

/**
 * @brief 健康检查
 */
bool healthCheck(const char *server, int port)
{
    int sock = connectToServer(server, port);
    if (sock < 0)
        return false;

    std::string httpRequest = "GET /health HTTP/1.1\r\n";
    httpRequest += "Host: localhost:8888\r\n";
    httpRequest += "Connection: close\r\n";
    httpRequest += "\r\n";

    if (send(sock, httpRequest.c_str(), httpRequest.size(), 0) < 0)
    {
        close(sock);
        return false;
    }

    char responseBuffer[1024];
    int recvLen = recv(sock, responseBuffer, sizeof(responseBuffer) - 1, 0);
    close(sock);

    if (recvLen <= 0)
        return false;

    responseBuffer[recvLen] = '\0';
    return strstr(responseBuffer, "\"status\":\"ok\"") != nullptr;
}

// ===================== 客户端线程函数 =====================

/**
 * @brief 客户端线程执行函数
 *
 * 每个线程连接到服务器并发送多个请求
 */
struct ClientThreadArgs
{
    const char *server;
    int port;
    int numRequests;
    uint64_t baseUserId;
};

void *clientThreadFunc(void *arg)
{
    ClientThreadArgs *args = (ClientThreadArgs *)arg;

    Stats localStats;
    localStats.reset();

    for (int i = 0; i < args->numRequests; i++)
    {
        int sock = connectToServer(args->server, args->port);
        if (sock < 0)
        {
            localStats.failedRequests++;
            continue;
        }

        uint64_t userId = args->baseUserId + i;
        int count = 10;
        std::string scene = (i % 3 == 0) ? "homepage" : (i % 3 == 1) ? "detail" : "search";

        int64_t latencyUs = 0;
        bool success = sendRecommendRequest(sock, userId, count, scene, latencyUs);
        close(sock);

        localStats.totalRequests++;
        if (success)
        {
            localStats.successfulRequests++;
            localStats.totalLatencyUs += latencyUs;
            localStats.minLatencyUs = std::min(localStats.minLatencyUs, latencyUs);
            localStats.maxLatencyUs = std::max(localStats.maxLatencyUs, latencyUs);

            if ((i + 1) % 10 == 0)
            {
                printf("Progress: %d/%d requests completed\n", i + 1, args->numRequests);
            }
        }
        else
        {
            localStats.failedRequests++;
        }

        // 小延迟，避免过于猛烈
        usleep(10000); // 10ms
    }

    // 累加统计到全局统计
    pthread_mutex_lock(&g_statsMutex);
    g_stats.totalRequests += localStats.totalRequests;
    g_stats.successfulRequests += localStats.successfulRequests;
    g_stats.failedRequests += localStats.failedRequests;
    g_stats.totalLatencyUs += localStats.totalLatencyUs;
    g_stats.minLatencyUs = std::min(g_stats.minLatencyUs, localStats.minLatencyUs);
    g_stats.maxLatencyUs = std::max(g_stats.maxLatencyUs, localStats.maxLatencyUs);
    pthread_mutex_unlock(&g_statsMutex);

    delete args;
    return nullptr;
}

// ===================== main函数 =====================

int main(int argc, char *argv[])
{
    const char *server = DEFAULT_SERVER;
    int port = DEFAULT_PORT;
    int numRequests = DEFAULT_REQUESTS;
    int numThreads = DEFAULT_THREADS;
    bool doHealthCheck = true;

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
        {
            server = argv[++i];
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            numRequests = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
        {
            numThreads = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-no-health") == 0)
        {
            doHealthCheck = false;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -s <server>      Server address (default: 127.0.0.1)\n");
            printf("  -p <port>        Server port (default: 8888)\n");
            printf("  -n <num>         Number of requests per thread (default: 100)\n");
            printf("  -t <num>         Number of threads (default: 1)\n");
            printf("  -no-health       Skip health check\n");
            printf("  -h, --help       Show this help message\n");
            return 0;
        }
    }

    printf("========================================\n");
    printf("Recommendation Service Test Client\n");
    printf("========================================\n");
    printf("Server:     %s:%d\n", server, port);
    printf("Threads:    %d\n", numThreads);
    printf("Requests:   %d per thread (total: %d)\n", numRequests, numRequests * numThreads);
    printf("========================================\n\n");

    // 健康检查
    if (doHealthCheck)
    {
        printf("Performing health check...\n");
        if (healthCheck(server, port))
        {
            printf("✓ Server is healthy\n\n");
        }
        else
        {
            fprintf(stderr, "✗ Server health check failed!\n");
            fprintf(stderr, "  Make sure the server is running on %s:%d\n", server, port);
            return 1;
        }
    }

    // 初始化统计
    g_stats.reset();

    // 启动客户端线程
    printf("Starting %d client threads...\n\n", numThreads);
    auto testStartTime = std::chrono::high_resolution_clock::now();

    std::vector<pthread_t> threads;
    for (int i = 0; i < numThreads; i++)
    {
        ClientThreadArgs *args = new ClientThreadArgs();
        args->server = server;
        args->port = port;
        args->numRequests = numRequests;
        args->baseUserId = 10000 + i * 1000; // 不同线程使用不同的userId范围

        pthread_t tid;
        if (pthread_create(&tid, nullptr, clientThreadFunc, args) != 0)
        {
            perror("pthread_create");
            delete args;
            return 1;
        }
        threads.push_back(tid);
    }

    // 等待所有线程完成
    for (auto tid : threads)
    {
        pthread_join(tid, nullptr);
    }

    auto testEndTime = std::chrono::high_resolution_clock::now();
    int64_t totalTestTimeUs =
        std::chrono::duration_cast<std::chrono::microseconds>(testEndTime - testStartTime).count();

    // 打印统计结果
    printf("\n");
    g_stats.print("Overall Test Results");
    printf("Total Test Duration:  %.2f seconds\n", (double)totalTestTimeUs / 1000000);
    if (totalTestTimeUs > 0)
    {
        printf("Actual Throughput:    %.2f req/s\n", (double)g_stats.successfulRequests * 1000000 / totalTestTimeUs);
    }

    return g_stats.failedRequests > 0 ? 1 : 0;
}
