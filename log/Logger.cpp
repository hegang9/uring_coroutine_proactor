#include "Logger.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

std::unique_ptr<Logger> Logger::instance_ = nullptr;
std::atomic<LogLevel> Logger::minLevel_(LogLevel::INFO);

void Logger::init(const Options &options)
{
    if (instance_)
    {
        return; // 已初始化
    }
    instance_.reset(new Logger(options));
    minLevel_.store(options.level);
}

void Logger::shutdown()
{
    if (instance_)
    {
        instance_->running_.store(false);
        if (instance_->worker_.joinable())
        {
            instance_->worker_.join();
        }
        instance_.reset();
    }
}

Logger::Logger(const Options &options)
    : options_(options), queue_(new LockFreeQueue<LogEntry>(65536)), running_(true), currentFileSize_(0)
{
    // 创建日志目录
    std::string dir = options_.logFile.substr(0, options_.logFile.find_last_of('/'));
    if (!dir.empty())
    {
        ::mkdir(dir.c_str(), 0755);
    }

    // 打开日志文件
    logFile_.open(options_.logFile, std::ios::app);
    if (logFile_.is_open())
    {
        logFile_.seekp(0, std::ios::end);
        currentFileSize_ = static_cast<size_t>(logFile_.tellp());
    }

    // 启动后台线程
    if (options_.async)
    {
        worker_ = std::thread(&Logger::backgroundThread, this);
    }
}

Logger::~Logger()
{
    running_.store(false);
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (logFile_.is_open())
    {
        logFile_.flush();
        logFile_.close();
    }
}

void Logger::backgroundThread()
{
    while (running_.load())
    {
        processEntries();
        std::this_thread::sleep_for(options_.flushInterval);
    }
    // 最后一次处理剩余日志
    processEntries();
}

void Logger::processEntries()
{
    LogEntry entry;
    int count = 0;
    const int maxBatch = 1000;

    while (count < maxBatch && queue_->dequeue(entry))
    {
        writeEntry(entry);
        count++;
    }

    if (count > 0 && logFile_.is_open())
    {
        logFile_.flush();
    }
}

void Logger::writeEntry(const LogEntry &entry)
{
    std::string timestamp = formatTimestamp(entry.timestampUs);
    std::string levelStr = levelToString(entry.level);

    // 提取文件名（去掉路径）
    const char *filename = entry.file;
    const char *slash = std::strrchr(entry.file, '/');
    if (slash)
    {
        filename = slash + 1;
    }

    // 格式：[时间] [级别] [线程ID] [文件:行] 消息
    std::string logLine = fmt::format("[{}] [{}] [{}] [{}:{}] {}\n", timestamp, levelStr, entry.threadId, filename,
                                      entry.line, entry.message);

    // 写入文件
    if (logFile_.is_open())
    {
        logFile_ << logLine;
        currentFileSize_ += logLine.size();
        rotateIfNeeded();
    }

    // 写入控制台
    if (options_.console)
    {
        if (entry.level >= LogLevel::WARN)
        {
            std::cerr << logLine;
        }
        else
        {
            std::cout << logLine;
        }
    }
}

void Logger::rotateIfNeeded()
{
    if (currentFileSize_ < options_.maxFileSize)
    {
        return;
    }

    logFile_.close();

    // 重命名旧日志文件
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    ::localtime_r(&timeT, &tm);

    std::string newName = fmt::format("{}.{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}", options_.logFile, tm.tm_year + 1900,
                                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    ::rename(options_.logFile.c_str(), newName.c_str());

    // 打开新日志文件
    logFile_.open(options_.logFile, std::ios::app);
    currentFileSize_ = 0;

    // TODO: 删除过期日志文件（保留最近 maxFiles 个）
}

std::string Logger::formatTimestamp(uint64_t timestampUs)
{
    uint64_t seconds = timestampUs / 1000000;
    uint64_t micros = timestampUs % 1000000;

    // 缓存秒级时间戳格式化结果
    if (timeCache_.lastSecond != seconds)
    {
        auto timeT = static_cast<std::time_t>(seconds);
        std::tm tm;
        ::localtime_r(&timeT, &tm);

        std::snprintf(timeCache_.buffer, sizeof(timeCache_.buffer), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        timeCache_.lastSecond = seconds;
    }

    return fmt::format("{}.{:06d}", timeCache_.buffer, micros);
}

std::string Logger::levelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO ";
    case LogLevel::WARN:
        return "WARN ";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}
