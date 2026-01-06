#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class EventLoop;

/**
 * 该类封装一个线程，在线程中运行一个 EventLoop 对象
 */

class EventLoopThread
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback());
    ~EventLoopThread();

    // 禁止拷贝和赋值
    EventLoopThread(const EventLoopThread &) = delete;
    EventLoopThread &operator=(const EventLoopThread &) = delete;

    EventLoop *startLoop();

private:
    // promise/future版本，开销较大
    // void threadFunc(std::promise<EventLoop *> &&p);
    void threadFunc();

    EventLoop *loop_; // 线程中的 EventLoop 对象
    bool exiting_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback callback_;
};
