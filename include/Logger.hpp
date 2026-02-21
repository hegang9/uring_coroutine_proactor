#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "LockFreeQueue.hpp"

enum class LogLevel
{
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

// 日志条目结构（固定大小，避免动态分配）
struct LogEntry
{
    LogLevel level;
    uint64_t timestampUs; // 微秒时间戳
    pid_t threadId;
    const char *file;
    int line;
    char message[512]; // 预分配消息缓冲区

    LogEntry() : level(LogLevel::INFO), timestampUs(0), threadId(0), file(nullptr), line(0), message{0}
    {
    }
};

class Logger
{
  public:
    struct Options
    {
        LogLevel level = LogLevel::INFO;
        std::string logFile = "logs/server.log";
        size_t maxFileSize = 100 * 1024 * 1024; // 100MB
        size_t maxFiles = 10;
        bool async = true;
        bool console = true;
        std::chrono::milliseconds flushInterval{1000};
    };

    static void init(const Options &options);
    static void shutdown();

    template <typename... Args>
    static void log(LogLevel level, const char *file, int line, const char *fmt, Args &&...args);

    static void setLevel(LogLevel level)
    {
        minLevel_.store(level);
    }
    static LogLevel getLevel()
    {
        return minLevel_.load();
    }

    ~Logger();

  private:
    Logger(const Options &options);

    void backgroundThread();
    void processEntries();
    void writeEntry(const LogEntry &entry);
    void rotateIfNeeded();
    std::string formatTimestamp(uint64_t timestampUs);
    std::string levelToString(LogLevel level);

    static std::unique_ptr<Logger> instance_;
    static std::atomic<LogLevel> minLevel_;

    Options options_;
    std::unique_ptr<LockFreeQueue<LogEntry>> queue_;
    std::thread worker_;
    std::atomic_bool running_;
    std::ofstream logFile_;
    size_t currentFileSize_;

    // 时间戳缓存（每秒更新一次）
    struct TimeCache
    {
        uint64_t lastSecond = 0;
        char buffer[32] = {0};
    };
    TimeCache timeCache_;
};

// 日志宏定义
#define LOG_TRACE(fmt, ...)                                                                                            \
    if (Logger::getLevel() <= LogLevel::TRACE)                                                                         \
    Logger::log(LogLevel::TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                            \
    if (Logger::getLevel() <= LogLevel::DEBUG)                                                                         \
    Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...)                                                                                             \
    if (Logger::getLevel() <= LogLevel::INFO)                                                                          \
    Logger::log(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                             \
    if (Logger::getLevel() <= LogLevel::WARN)                                                                          \
    Logger::log(LogLevel::WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                                            \
    if (Logger::getLevel() <= LogLevel::ERROR)                                                                         \
    Logger::log(LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...)                                                                                            \
    if (Logger::getLevel() <= LogLevel::FATAL)                                                                         \
    Logger::log(LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// 模板实现（需要在头文件中）
#include <fmt/core.h>
#include <sys/syscall.h>
#include <unistd.h>

template <typename... Args>
void Logger::log(LogLevel level, const char *file, int line, const char *fmtStr, Args &&...args)
{
    if (!instance_ || level < minLevel_.load())
    {
        return;
    }

    LogEntry entry;
    entry.level = level;
    entry.threadId = static_cast<pid_t>(::syscall(SYS_gettid));
    entry.file = file;
    entry.line = line;

    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    entry.timestampUs = static_cast<uint64_t>(us);

    try
    {
        auto msg = fmt::format(fmt::runtime(fmtStr), std::forward<Args>(args)...);
        size_t copyLen = std::min(msg.size(), sizeof(entry.message) - 1);
        std::memcpy(entry.message, msg.c_str(), copyLen);
        entry.message[copyLen] = '\0';
    }
    catch (const std::exception &e)
    {
        std::snprintf(entry.message, sizeof(entry.message), "[fmt error: %s]", e.what());
    }

    if (!instance_->queue_->enqueue(entry))
    {
        // 队列满，降级到同步输出（仅控制台）
        if (instance_->options_.console)
        {
            std::fprintf(stderr, "[QUEUE_FULL] %s\n", entry.message);
        }
    }
}
