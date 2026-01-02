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
        std::suspend_never final_suspend() noexcept { return {}; } // 协程结束后不挂起
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle_;

    Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ~Task()
    {
        // 由于 final_suspend 返回 suspend_never，协程会自动销毁，
        // 这里不需要手动 destroy，除非我们改变了 final_suspend 的行为。
        // 注意：如果协程被挂起且未完成，handle 需要被妥善管理。
        // 在这个简单的实现中，我们假设协程是 "fire and forget" 或者由调用者管理生命周期。
    }
};
