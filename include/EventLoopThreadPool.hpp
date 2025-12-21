#pragma once

#include "Noncopyable.hpp"
#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : private Noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThreadPool(EventLoop *baseLoop);
    ~EventLoopThreadPool();

    // 设置线程数量
    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    // 启动线程池
    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 如果工作在多线程中，轮询获取下一个 EventLoop用作子线程的 EventLoop
    EventLoop *getNextLoop();

    std::vector<EventLoop *> getAllLoops();

    bool started() const { return started_; }

private:
    EventLoop *baseLoop_; // 主线程的 loop
    bool started_;
    int numThreads_;
    int next_;                                              // 下一个 loop 的索引
    std::vector<std::unique_ptr<EventLoopThread>> threads_; // 线程池，使用unique_ptr独占所有权，并自动管理生命周期
    std::vector<EventLoop *> loops_;                        // 子线程的 loop 列表
};
