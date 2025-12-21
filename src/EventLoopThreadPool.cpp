#include "EventLoopThreadPool.hpp"
#include "EventLoopThread.hpp"
#include "EventLoop.hpp"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop)
    : baseLoop_(baseLoop),
      started_(false),
      numThreads_(0),
      next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // 不需要手动释放 loop，因为它们是在栈上分配的（在 EventLoopThread::threadFunc 中）
}

// 根据设定的线程数，创建并启动相应数量的子线程，同时收集这些子线程中运行的 EventLoop 指针
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;

    for (int i = 0; i < numThreads_; ++i)
    {
        auto t = std::make_unique<EventLoopThread>(cb);
        loops_.push_back(t->startLoop());
        threads_.push_back(std::move(t));
    }

    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    EventLoop *loop = baseLoop_;

    if (!loops_.empty())
    {
        // 轮询
        loop = loops_[next_];
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size()) // 环形
        {
            next_ = 0;
        }
    }

    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}
