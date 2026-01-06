#pragma once

#include <coroutine>
#include <exception>
#include <utility>

/**
 * @brief 简单的协程任务类 (Coroutine Return Object)
 * 用于封装协程的状态和生命周期
 */
struct Task
{
    struct promise_type
    {
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() { return {}; }        // 协程创建后立即执行
        std::suspend_never final_suspend() noexcept { return {}; } // 协程结束后不挂起，立即销毁自身
        void return_void() {}                                      // 表示协程无返回值
        void unhandled_exception() { std::terminate(); }           // 协程出错时终止程序
    };

    std::coroutine_handle<promise_type> handle_;

    Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ~Task()
    {
        // 由于 final_suspend 返回 suspend_never，协程会自动销毁，
        // 这里不需要手动 destroy，除非我们改变了 final_suspend 的行为。
    }
};
