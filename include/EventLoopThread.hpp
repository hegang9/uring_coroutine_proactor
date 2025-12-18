#pragma once

#include "Noncopyable.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class EventLoop;

/**
 * 该类封装一个线程，在线程中运行一个 EventLoop 对象
 */

class EventLoopThread : private Noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback());
    ~EventLoopThread();

    EventLoop *startLoop();

private:
    void threadFunc();

    EventLoop *loop_;
    bool exiting_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback callback_;
};
