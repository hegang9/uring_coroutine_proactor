#include "EventLoopThread.hpp"

#include <iostream>
#include <thread>

#include "EventLoop.hpp"

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb)
    : loop_(nullptr), exiting_(false), callback_(cb) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  if (loop_ != nullptr) {
    loop_->quit();
    thread_.join();  // 等待线程退出，确保资源安全释放
  }
}

EventLoop* EventLoopThread::startLoop() {
  thread_ = std::thread(std::bind(&EventLoopThread::threadFunc, this));

  EventLoop* loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (loop_ == nullptr) {
      cond_.wait(lock);
    }
    loop = loop_;
    loop->initRegisteredBuffers();
  }
  return loop;
}

void EventLoopThread::threadFunc() {
  EventLoop loop;  // 栈上创建EventLoop对象

  std::cout << "[Thread] EventLoop thread start, tid="
            << std::this_thread::get_id() << ", loop=" << &loop << std::endl;

  if (callback_) {
    callback_(&loop);
  }

  {
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }

  loop.loop();

  std::cout << "[Thread] EventLoop thread exit, tid="
            << std::this_thread::get_id() << std::endl;

  std::lock_guard<std::mutex> lock(mutex_);
  loop_ = nullptr;
}

/*
EventLoop *EventLoopThread::startLoop()
{
    std::promise<EventLoop *> p;
    std::future<EventLoop *> f = p.get_future();

    thread_ = std::thread(std::bind(&EventLoopThread::threadFunc, this,
std::move(p)));

    EventLoop *loop = f.get(); // 阻塞等待直到 promise 设置值
    return loop;
}

void EventLoopThread::threadFunc(std::promise<EventLoop *> &&p)
{
    EventLoop loop; // 栈上创建EventLoop对象

    if (callback_)
    {
        callback_(&loop); // 允许用户在 Loop 开始循环前做一些定制化设置
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
    }

    // 通知主线程 loop 已经初始化完毕
    p.set_value(&loop);

    // 开始循环
    loop.loop();

    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
*/