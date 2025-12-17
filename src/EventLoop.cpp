#include "EventLoop.hpp"
#include <sys/eventfd.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>

// 获取当前线程ID的辅助函数 (Linux specific)
#include <sys/syscall.h>
static pid_t gettid()
{
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

EventLoop::EventLoop()
    : running_(false),
      quit_(false),
      threadId_(gettid()),
      wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      wakeupContext_(IoType::Read, wakeupFd_),
      callingPendingFunctors_(false)
{
    if (wakeupFd_ < 0)
    {
        perror("eventfd");
        abort();
    }

    // 初始化 io_uring，队列深度设为 4096，这里不设置IORING_SETUP_SQPOLL，因为在多线程环境下会产生多个内核线程专门用于轮询，这样可能会严重抢占业务逻辑的CPU时间片，导致上下文切换频繁，反而降低性能
    int ret = io_uring_queue_init(4096, &ring_, 0);
    if (ret < 0)
    {
        fprintf(stderr, "io_uring_queue_init failed: %d\n", ret);
        abort();
    }

    // 设置 io_uring I/O完成的回调
    wakeupContext_.handler = std::bind(&EventLoop::handleWakeup, this);

    // 提交第一个 wakeup 读请求
    asyncReadWakeup();
}

EventLoop::~EventLoop()
{
    ::close(wakeupFd_);
    io_uring_queue_exit(&ring_);
}

void EventLoop::loop()
{
    running_ = true;
    quit_ = false;

    while (!quit_)
    {
        struct io_uring_cqe *cqe;
        // 等待至少一个事件完成
        int ret = io_uring_wait_cqe(&ring_, &cqe);

        if (ret < 0)
        {
            if (ret == -EINTR)
                continue; // 被信号中断
            fprintf(stderr, "io_uring_wait_cqe error: %d\n", ret);
            break;
        }

        // 处理完成队列中的所有事件
        unsigned head;
        unsigned count = 0;

        // 批量遍历 CQE
        io_uring_for_each_cqe(&ring_, head, cqe)
        {
            count++;
            IoContext *ctx = static_cast<IoContext *>(io_uring_cqe_get_data(cqe));
            if (ctx && ctx->handler)
            {
                // 调用回调函数，传入 res (结果)
                // 这个回调函数包括 Acceptor::handleRead, EventLoop::handleWakeup 两种，一个用于读，一个用于被唤醒
                ctx->handler(cqe->res);
            }
        }

        // 推进 CQ 队列
        io_uring_cq_advance(&ring_, count);

        // 执行任务队列中的任务
        doPendingFunctors();
    }

    running_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    if (gettid() != threadId_)
    {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb)
{
    if (gettid() == threadId_)
    {
        cb();
    }
    else
    {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }

    // 如果不在当前线程，或者当前正在执行 pendingFunctors，都需要唤醒
    if (gettid() != threadId_ || callingPendingFunctors_)
    {
        wakeup();
    }
}

// 写eventfd以唤醒子线程EventLoop循环
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        perror("EventLoop::wakeup write");
    }
}

void EventLoop::handleWakeup()
{
    uint64_t one = 1;
    // 读取 eventfd，清除唤醒信号
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        perror("EventLoop::handleWakeup read");
    }

    // 重新提交 wakeup 读请求，以便下一次唤醒
    asyncReadWakeup();
}

void EventLoop::asyncReadWakeup()
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (!sqe)
    {
        // 极其罕见的情况：SQ 满了。
        // 实际生产中可能需要处理，这里简单打印
        fprintf(stderr, "EventLoop::asyncReadWakeup: SQ full\n");
        return;
    }

    // 准备读取 eventfd
    io_uring_prep_read(sqe, wakeupFd_, &wakeupContext_.buffer, sizeof(uint64_t), 0);
    io_uring_sqe_set_data(sqe, &wakeupContext_);
    io_uring_submit(&ring_);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const auto &func : functors)
    {
        func();
    }

    callingPendingFunctors_ = false;
}
