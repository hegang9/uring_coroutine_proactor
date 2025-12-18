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

    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 如果工作在多线程中，baseLoop_默认以轮询的方式分配channel给subloop
    EventLoop *getNextLoop();

    std::vector<EventLoop *> getAllLoops();

    bool started() const { return started_; }

private:
    EventLoop *baseLoop_; // 主线程的 loop
    bool started_;
    int numThreads_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop *> loops_;
};
