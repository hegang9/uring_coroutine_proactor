#include "EventLoopThread.hpp"
#include "EventLoop.hpp"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb)
    : loop_(nullptr),
      exiting_(false),
      callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join(); // 等待线程退出，确保资源安全释放
    }
}

EventLoop *EventLoopThread::startLoop()
{
    thread_ = std::thread(std::bind(&EventLoopThread::threadFunc, this));

    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr)
        {
            cond_.wait(lock); // thread_还没有初始化EventLoop，必须等待
        }
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop; // 栈上创建EventLoop对象

    if (callback_)
    {
        callback_(&loop); // 允许用户在 Loop 开始循环前做一些定制化设置
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    // 开始循环
    loop.loop();

    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
