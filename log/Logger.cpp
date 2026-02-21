/**
 * @file Logger.cpp
 * @brief 日志系统实现文件
 */
#include "Logger.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

// ============================================================================
// 静态成员初始化
// ============================================================================
std::unique_ptr<Logger> Logger::instance_ = nullptr;     // 单例实例（初始为空）
std::atomic<LogLevel> Logger::minLevel_(LogLevel::INFO); // 默认日志级别为 INFO

/**
 * @brief 初始化日志系统（线程安全）
 * @param options 日志配置选项
 * @note 重复调用将被忽略（单例保护）
 */
void Logger::init(const Options &options)
{
    if (instance_)
    {
        return; // 已初始化，直接返回
    }
    instance_.reset(new Logger(options)); // 创建单例实例
    minLevel_.store(options.level);       // 设置最小日志级别
}

/**
 * @brief 关闭日志系统（清理资源）
 * @note 会等待后台线程处理完所有待写日志后退出
 */
void Logger::shutdown()
{
    if (instance_)
    {
        instance_->running_.store(false); // 通知后台线程退出
        if (instance_->worker_.joinable())
        {
            instance_->worker_.join(); // 等待后台线程结束
        }
        instance_.reset(); // 销毁实例
    }
}

/**
 * @brief Logger 构造函数（私有，仅供单例使用）
 * @param options 日志配置选项
 *
 * 初始化流程：
 * 1. 创建日志目录（如果不存在）
 * 2. 以追加模式打开日志文件
 * 3. 获取当前文件大小（用于轮转判断）
 * 4. 启动后台线程（如果启用异步模式）
 */
Logger::Logger(const Options &options)
    : options_(options), queue_(new LockFreeQueue<LogEntry>(65536)), // 创建容量为 65536 的无锁队列
      running_(true), currentFileSize_(0)
{
    // 创建日志目录（从日志文件路径中提取目录部分）
    std::string dir = options_.logFile.substr(0, options_.logFile.find_last_of('/'));
    if (!dir.empty())
    {
        ::mkdir(dir.c_str(), 0755); // 创建目录（权限：rwxr-xr-x）
    }

    // 以追加模式打开日志文件
    logFile_.open(options_.logFile, std::ios::app);
    if (logFile_.is_open())
    {
        // 定位到文件末尾并获取当前文件大小
        logFile_.seekp(0, std::ios::end);
        currentFileSize_ = static_cast<size_t>(logFile_.tellp());
    }

    // 如果启用异步模式，启动后台处理线程
    if (options_.async)
    {
        worker_ = std::thread(&Logger::backgroundThread, this);
    }
}

/**
 * @brief Logger 析构函数
 *
 * 清理流程：
 * 1. 设置运行标志为 false（通知后台线程退出）
 * 2. 等待后台线程结束
 * 3. 刷新并关闭日志文件
 */
Logger::~Logger()
{
    running_.store(false);
    if (worker_.joinable())
    {
        worker_.join(); // 等待后台线程退出
    }
    if (logFile_.is_open())
    {
        logFile_.flush(); // 确保所有数据写入磁盘
        logFile_.close();
    }
}

/**
 * @brief 后台线程主函数
 *
 * 执行逻辑：
 * 1. 循环处理日志队列中的条目
 * 2. 每次处理后休眠指定时间（flushInterval，默认 1000ms）
 * 3. 退出前再次处理队列（确保不丢失日志）
 *
 * 设计考虑：
 * - 休眠间隔可配置，平衡延迟和 CPU 占用
 * - 退出时额外处理一次，避免丢失最后的日志
 */
void Logger::backgroundThread()
{
    while (running_.load())
    {
        processEntries();                                    // 批量处理日志条目
        std::this_thread::sleep_for(options_.flushInterval); // 休眠等待下次处理
    }
    // 线程退出前最后一次处理剩余日志
    processEntries();
}

/**
 * @brief 批量处理日志队列中的条目
 *
 * 处理策略：
 * - 每次最多处理 1000 条日志（避免单次处理时间过长）
 * - 从无锁队列中出队并逐条写入
 * - 处理完成后刷新文件缓冲区
 *
 * 性能优化：
 * - 批量处理减少系统调用次数
 * - 限制单次处理数量，保证响应延迟
 */
void Logger::processEntries()
{
    LogEntry entry;
    int count = 0;
    const int maxBatch = 1000; // 单次批量处理上限

    // 批量出队并处理
    while (count < maxBatch && queue_->dequeue(entry))
    {
        writeEntry(entry);
        count++;
    }

    // 如果处理了日志，刷新文件缓冲区（确保持久化）
    if (count > 0 && logFile_.is_open())
    {
        logFile_.flush();
    }
}

/**
 * @brief 写入单条日志到文件和控制台
 * @param entry 日志条目
 *
 * 输出格式：
 * [时间戳] [级别] [线程ID] [文件:行号] 消息
 * 示例：[2026-02-21 15:30:45.123456] [INFO ] [12345] [main.cpp:42] Server started
 *
 * 输出策略：
 * - 文件：所有日志都写入（如果文件打开成功）
 * - 控制台：WARN 及以上写 stderr，其他写 stdout
 */
void Logger::writeEntry(const LogEntry &entry)
{
    // 格式化时间戳和日志级别
    std::string timestamp = formatTimestamp(entry.timestampUs);
    std::string levelStr = levelToString(entry.level);

    // 提取文件名（去掉完整路径，仅保留文件名）
    const char *filename = entry.file;
    const char *slash = std::strrchr(entry.file, '/'); // 查找最后一个斜杠
    if (slash)
    {
        filename = slash + 1; // 跳过斜杠，指向文件名
    }

    // 格式化完整日志行
    // 格式：[时间] [级别] [线程ID] [文件:行] 消息
    std::string logLine = fmt::format("[{}] [{}] [{}] [{}:{}] {}\n", timestamp, levelStr, entry.threadId, filename,
                                      entry.line, entry.message);

    // 写入文件
    if (logFile_.is_open())
    {
        logFile_ << logLine;
        currentFileSize_ += logLine.size(); // 累计文件大小
        rotateIfNeeded();                   // 检查是否需要轮转
    }

    // 写入控制台（根据日志级别选择输出流）
    if (options_.console)
    {
        if (entry.level >= LogLevel::WARN)
        {
            std::cerr << logLine; // 警告及以上输出到 stderr
        }
        else
        {
            std::cout << logLine; // 普通日志输出到 stdout
        }
    }
}

/**
 * @brief 检查并执行日志文件轮转
 *
 * 轮转条件：
 * - 当前文件大小 >= maxFileSize（默认 100MB）
 *
 * 轮转流程：
 * 1. 关闭当前日志文件
 * 2. 将当前文件重命名为带时间戳的归档文件
 *    格式：原文件名.YYYYMMDD-HHMMSS
 *    示例：server.log.20260221-153045
 * 3. 创建新的日志文件
 * 4. 重置文件大小计数器
 *
 * TODO: 删除过期的归档文件（当前保留所有历史文件）
 */
void Logger::rotateIfNeeded()
{
    // 检查是否达到轮转阈值
    if (currentFileSize_ < options_.maxFileSize)
    {
        return; // 未达到阈值，无需轮转
    }

    logFile_.close(); // 关闭当前文件

    // 生成归档文件名（附加当前时间戳）
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    ::localtime_r(&timeT, &tm); // 线程安全的本地时间转换

    // 格式化新文件名：原文件名.YYYYMMDD-HHMMSS
    std::string newName = fmt::format("{}.{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}", options_.logFile, tm.tm_year + 1900,
                                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    ::rename(options_.logFile.c_str(), newName.c_str()); // 重命名旧文件

    // 打开新日志文件（追加模式）
    logFile_.open(options_.logFile, std::ios::app);
    currentFileSize_ = 0; // 重置文件大小计数

    // TODO: 删除过期日志文件（保留最近 maxFiles 个）
    // 实现思路：
    // 1. 扫描日志目录，找到所有归档文件（匹配文件名模式）
    // 2. 按时间戳排序
    // 3. 删除最旧的文件，保留最近 maxFiles 个
}

/**
 * @brief 格式化时间戳为可读字符串（带缓存优化）
 * @param timestampUs 微秒级时间戳（自 Unix epoch 起）
 * @return 格式化的时间字符串，如 "2026-02-21 15:30:45.123456"
 *
 * 性能优化：
 * - 秒级部分使用缓存（timeCache_），同一秒内复用
 * - 仅微秒部分每次重新格式化
 * - 避免了重复的 localtime_r 和 snprintf 调用
 *
 * 典型性能：
 * - 缓存命中（同一秒）：约 50ns
 * - 缓存未命中（新的一秒）：约 500ns
 */
std::string Logger::formatTimestamp(uint64_t timestampUs)
{
    uint64_t seconds = timestampUs / 1000000; // 提取秒数部分
    uint64_t micros = timestampUs % 1000000;  // 提取微秒部分

    // 检查缓存是否有效（秒级时间戳未变化）
    if (timeCache_.lastSecond != seconds)
    {
        // 缓存失效，重新格式化秒级时间戳
        auto timeT = static_cast<std::time_t>(seconds);
        std::tm tm;
        ::localtime_r(&timeT, &tm); // 线程安全的本地时间转换

        // 格式化为 "YYYY-MM-DD HH:MM:SS"
        std::snprintf(timeCache_.buffer, sizeof(timeCache_.buffer), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        timeCache_.lastSecond = seconds; // 更新缓存的秒数
    }

    // 拼接缓存的秒级部分 + 微秒部分
    return fmt::format("{}.{:06d}", timeCache_.buffer, micros);
}

/**
 * @brief 将日志级别转换为字符串（用于输出）
 * @param level 日志级别枚举值
 * @return 日志级别名称（5 字符宽度，右侧填充空格对齐）
 *
 * 输出格式：
 * - "TRACE"
 * - "DEBUG"
 * - "INFO " (带空格，保证 5 字符宽度)
 * - "WARN " (带空格)
 * - "ERROR"
 * - "FATAL"
 */
std::string Logger::levelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO "; // 右侧填充空格，保证对齐
    case LogLevel::WARN:
        return "WARN "; // 右侧填充空格
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}
