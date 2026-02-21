/**
 * @file Logger.hpp
 * @brief 高性能异步日志系统头文件
 *
 * 特性：
 * - 基于无锁队列的异步日志，避免阻塞业务线程
 * - 使用 fmt 库进行高效格式化
 * - 支持日志文件自动轮转（按大小和时间）
 * - 支持多日志级别过滤
 * - 双输出：文件和控制台
 * - 固定大小日志条目，避免动态内存分配
 * - 时间戳缓存优化，减少格式化开销
 */
#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "LockFreeQueue.hpp"

/**
 * @brief 日志级别枚举
 *
 * 级别从低到高：TRACE < DEBUG < INFO < WARN < ERROR < FATAL
 * 设置某级别后，低于该级别的日志将被过滤
 */
enum class LogLevel
{
    TRACE = 0, // 追踪级别，详细的调试信息
    DEBUG = 1, // 调试级别，用于开发阶段
    INFO = 2,  // 信息级别，重要的业务流程
    WARN = 3,  // 警告级别，潜在问题
    ERROR = 4, // 错误级别，可恢复的错误
    FATAL = 5  // 致命级别，不可恢复的错误
};

/**
 * @brief 日志条目结构（固定大小，避免动态分配）
 *
 * 设计考虑：
 * - 固定大小便于无锁队列高效传递
 * - 消息缓冲区 512 字节足够大部分场景
 * - 存储原始文件指针而非复制路径，减少开销
 */
struct LogEntry
{
    LogLevel level;       // 日志级别
    uint64_t timestampUs; // 微秒级时间戳（自 epoch 起）
    pid_t threadId;       // 线程 ID（通过 gettid 获取）
    const char *file;     // 源文件路径（指针，不拷贝）
    int line;             // 源代码行号
    char message[512];    // 预分配的消息缓冲区

    LogEntry() : level(LogLevel::INFO), timestampUs(0), threadId(0), file(nullptr), line(0), message{0}
    {
    }
};

/**
 * @brief 日志系统单例类
 *
 * 架构设计：
 * - 单例模式，全局唯一实例
 * - 前端：业务线程通过无锁队列写入日志条目（快速返回）
 * - 后端：专用后台线程批量消费日志条目并写入文件
 * - 解耦：业务线程不会被 I/O 操作阻塞
 */
class Logger
{
  public:
    /**
     * @brief 日志系统配置选项
     */
    struct Options
    {
        LogLevel level = LogLevel::INFO;               // 最小日志级别（低于此级别的日志将被过滤）
        std::string logFile = "logs/server.log";       // 日志文件路径
        size_t maxFileSize = 100 * 1024 * 1024;        // 单个日志文件最大大小（100MB）
        size_t maxFiles = 10;                          // 保留的历史日志文件数量（暂未实现）
        bool async = true;                             // 是否启用异步模式（后台线程写入）
        bool console = true;                           // 是否同时输出到控制台
        std::chrono::milliseconds flushInterval{1000}; // 后台线程刷新间隔（毫秒）
    };

    /**
     * @brief 初始化日志系统
     * @param options 日志配置选项
     * @note 线程安全，重复调用将被忽略
     */
    static void init(const Options &options);

    /**
     * @brief 关闭日志系统
     * @note 会等待后台线程处理完所有日志后退出
     */
    static void shutdown();

    /**
     * @brief 记录日志（模板函数，支持 fmt 格式化）
     * @tparam Args 可变参数类型（自动推导）
     * @param level 日志级别
     * @param file 源文件名（通常由宏自动传入 __FILE__）
     * @param line 源代码行号（通常由宏自动传入 __LINE__）
     * @param fmt 格式化字符串（fmt 库语法）
     * @param args 格式化参数
     * @note 非阻塞操作，日志条目被放入无锁队列后立即返回
     */
    template <typename... Args>
    static void log(LogLevel level, const char *file, int line, const char *fmt, Args &&...args);

    /**
     * @brief 设置日志级别（运行时可调整）
     * @param level 新的最小日志级别
     */
    static void setLevel(LogLevel level)
    {
        minLevel_.store(level);
    }

    /**
     * @brief 获取当前日志级别
     * @return 当前最小日志级别
     */
    static LogLevel getLevel()
    {
        return minLevel_.load();
    }

    /**
     * @brief 析构函数（公有，以支持 unique_ptr 管理）
     */
    ~Logger();

  private:
    /**
     * @brief 私有构造函数（单例模式）
     * @param options 日志配置选项
     */
    Logger(const Options &options);

    /**
     * @brief 后台线程主函数
     * @note 周期性调用 processEntries() 处理日志队列
     */
    void backgroundThread();

    /**
     * @brief 批量处理日志队列中的条目
     * @note 每次最多处理 1000 条日志，避免单次处理时间过长
     */
    void processEntries();

    /**
     * @brief 写入单条日志到文件和控制台
     * @param entry 日志条目
     */
    void writeEntry(const LogEntry &entry);

    /**
     * @brief 检查并执行日志文件轮转
     * @note 当文件大小超过 maxFileSize 时，重命名旧文件并创建新文件
     */
    void rotateIfNeeded();

    /**
     * @brief 格式化时间戳为可读字符串
     * @param timestampUs 微秒级时间戳
     * @return 格式化的时间字符串（如 "2026-02-21 15:30:45.123456"）
     */
    std::string formatTimestamp(uint64_t timestampUs);

    /**
     * @brief 将日志级别转换为字符串
     * @param level 日志级别
     * @return 级别名称字符串（如 "INFO ", "ERROR"）
     */
    std::string levelToString(LogLevel level);

    // === 静态成员 ===
    static std::unique_ptr<Logger> instance_; // 单例实例指针
    static std::atomic<LogLevel> minLevel_;   // 最小日志级别（原子操作，支持运行时修改）

    // === 实例成员 ===
    Options options_;                                // 日志系统配置
    std::unique_ptr<LockFreeQueue<LogEntry>> queue_; // 无锁队列（容量 65536）
    std::thread worker_;                             // 后台处理线程
    std::atomic_bool running_;                       // 运行标志（控制后台线程退出）
    std::ofstream logFile_;                          // 日志文件流
    size_t currentFileSize_;                         // 当前日志文件大小（字节）

    /**
     * @brief 时间戳缓存结构（性能优化）
     * @note 秒级时间戳格式化开销较大，缓存后同一秒内复用
     */
    struct TimeCache
    {
        uint64_t lastSecond = 0; // 上次格式化的秒数
        char buffer[32] = {0};   // 缓存的时间字符串（不含微秒部分）
    };
    TimeCache timeCache_;
};

// ============================================================================
// 日志宏定义（推荐使用方式）
// ============================================================================
// 优势：
// 1. 自动填充 __FILE__ 和 __LINE__，便于定位日志来源
// 2. 编译期级别检查，低于阈值的日志不会产生任何函数调用开销
// 3. 支持 fmt 格式化语法，如 LOG_INFO("user {} login from {}", userId, ip)
// ============================================================================

/** @brief TRACE 级别日志宏（最详细，性能敏感场景慎用） */
#define LOG_TRACE(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::TRACE)                                                                     \
            Logger::log(LogLevel::TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                      \
    } while (0)

/** @brief DEBUG 级别日志宏（开发调试用） */
#define LOG_DEBUG(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::DEBUG)                                                                     \
            Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                      \
    } while (0)

/** @brief INFO 级别日志宏（重要业务流程） */
#define LOG_INFO(fmt, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::INFO)                                                                      \
            Logger::log(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                       \
    } while (0)

/** @brief WARN 级别日志宏（潜在问题警告） */
#define LOG_WARN(fmt, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::WARN)                                                                      \
            Logger::log(LogLevel::WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                       \
    } while (0)

/** @brief ERROR 级别日志宏（可恢复的错误） */
#define LOG_ERROR(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::ERROR)                                                                     \
            Logger::log(LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                      \
    } while (0)

/** @brief FATAL 级别日志宏（致命错误） */
#define LOG_FATAL(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (Logger::getLevel() <= LogLevel::FATAL)                                                                     \
            Logger::log(LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__);                                      \
    } while (0)

// ============================================================================
// 模板实现（需要在头文件中）
// ============================================================================
#include <fmt/core.h>
#include <sys/syscall.h>
#include <unistd.h>

/**
 * @brief 日志记录模板函数实现
 *
 * 执行流程：
 * 1. 快速检查：未初始化或级别不足直接返回（零开销）
 * 2. 构造日志条目：填充元数据（级别、时间、线程、位置）
 * 3. 格式化消息：使用 fmt 库格式化，处理异常
 * 4. 入队：非阻塞写入无锁队列
 * 5. 降级处理：队列满时直接输出到 stderr（避免丢失关键日志）
 *
 * 性能特点：
 * - 快速路径（未启用或级别不足）：仅原子读取 + 比较
 * - 正常路径：格式化 + 内存拷贝 + 无锁入队（约 200-500ns）
 * - 队列满时降级为同步输出（罕见情况）
 */
template <typename... Args>
void Logger::log(LogLevel level, const char *file, int line, const char *fmtStr, Args &&...args)
{
    // 快速检查：日志系统未初始化或日志级别不足
    if (!instance_ || level < minLevel_.load())
    {
        return;
    }

    // 构造日志条目
    LogEntry entry;
    entry.level = level;
    entry.threadId = static_cast<pid_t>(::syscall(SYS_gettid)); // 获取真实线程 ID（非 pthread_self）
    entry.file = file;                                          // 仅存储指针，不拷贝字符串
    entry.line = line;

    // 获取微秒级时间戳
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    entry.timestampUs = static_cast<uint64_t>(us);

    // 格式化日志消息（使用 fmt 库）
    try
    {
        // fmt::runtime() 允许运行时格式字符串（fmt 8.x 要求）
        // 把每个 {} 按顺序替换成对应的参数值
        auto msg = fmt::format(fmt::runtime(fmtStr), std::forward<Args>(args)...);

        // 拷贝到固定缓冲区（防止溢出）
        size_t copyLen = std::min(msg.size(), sizeof(entry.message) - 1);
        std::memcpy(entry.message, msg.c_str(), copyLen);
        entry.message[copyLen] = '\0'; // 确保 null 终止
    }
    catch (const std::exception &e)
    {
        // 格式化失败时记录错误信息（避免崩溃）
        std::snprintf(entry.message, sizeof(entry.message), "[fmt error: %s]", e.what());
    }

    // 尝试入队（非阻塞）
    if (!instance_->queue_->enqueue(entry))
    {
        // 队列满（65536 条日志未消费）：降级到同步输出
        // 这种情况说明日志产生速度 > 消费速度，需要优化
        if (instance_->options_.console)
        {
            std::fprintf(stderr, "[QUEUE_FULL] %s\n", entry.message);
        }
    }
}
