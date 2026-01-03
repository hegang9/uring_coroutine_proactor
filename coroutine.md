## 协程
C++20引入，协程是一种轻量级的并发模型，允许在单个线程中暂停和恢复执行，本质是可以暂停和恢复的函数。C++20引入的是**非对称的无栈协程**，

#### 进程、线程、协程对比：
| 对比维度      | 进程                             | 线程                                     | 协程                                             |
| ------------- | -------------------------------- | ---------------------------------------- | ------------------------------------------------ |
| 本质          | 资源分配的基本单位               | 操作系统调度和执行的基本单位             | 语言级的轻量级控制流机制，是可暂停和恢复的函数   |
| 资源开销      | 大，需要独立内存空间、文件句柄等 | 中，需要分配栈和寄存器                   | 极小，仅需保留少量上下文如挂起点地址、局部变量等 |
| 调度方式      | 内核抢占式调度                   | 内核抢占式调度                           | 用户态协作式调度（由程序员或框架显式控制）       |
| 通信/数据共享 | 复杂，需要使用管道、消息队列等   | 简单，直接共享进程内存                   | 简单，通过局部变量、引用等直接访问               |
| 切换成本      | 高，需要切换地址空间，刷新TLB等  | 中，需保存和恢复寄存器、栈               | 极低，仅需保存寄存器上下文和栈，不涉及内核       |
| 可靠性/隔离性 | 强隔离，互不影响                 | 弱隔离，一个线程崩溃可能导致整个进程崩溃 | 依赖所在线程，协程异常可能导致线程异常           |

#### 协程的组成部分
| 组件                          | 角色与职责                                                                                                                                                                                              |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 协程返回值类型(Return Object) | 协程函数的返回类型。它通过内部的 promise_type定义协程的行为。                                                                                                                                           |
| 承诺类型(promise_type)        | 协程的控制中心，定义在返回值类型内部。它负责协程的生命周期关键节点行为，例如初始挂起、最终挂起、返回值处理、异常处理等                                                                                  |
| 等待体 (Awaiter)              | 真正实现挂起与恢复逻辑的对象。当协程执行 co_await expr时，expr需要转换为一个 Awaiter 对象，该对象必须实现三个核心方法：<br>await_ready()、await_suspend(std::coroutine_handle<> handle)、await_resume() |

#### 核心组件详解

##### 1. Promise Type (承诺对象)
`promise_type` 是协程的“控制中心”，它必须定义在协程返回对象的内部（或者通过 `std::coroutine_traits` 特化）。编译器依赖它来控制协程的启动、结束、返回值和异常处理。

**必须实现的方法：**

| 方法名 | 原型示例 | 作用与触发时机 |
| :--- | :--- | :--- |
| `get_return_object` | `Task get_return_object()` | **协程创建时调用**。用于创建协程函数的返回值对象（如 `Task`）。通常在此处通过 `std::coroutine_handle::from_promise(*this)` 获取句柄并传递给返回值对象。 |
| `initial_suspend` | `auto initial_suspend()` | **协程启动前调用**。返回一个 Awaiter。<br>- 返回 `std::suspend_always`：协程创建后立即挂起（懒启动）。<br>- 返回 `std::suspend_never`：协程创建后立即开始执行（贪婪启动）。 |
| `final_suspend` | `auto final_suspend() noexcept` | **协程执行结束时调用**。返回一个 Awaiter。<br>- 返回 `std::suspend_always`：协程结束后保持挂起状态，直到句柄被销毁。允许外部检查状态或获取返回值。<br>- 返回 `std::suspend_never`：协程结束后立即销毁自身（慎用，可能导致 Use-After-Free）。 |
| `unhandled_exception` | `void unhandled_exception()` | **协程体内抛出未捕获异常时调用**。通常在此处调用 `std::current_exception()` 捕获异常并保存，以便稍后重新抛出。 |
| `return_void` | `void return_void()` | **协程执行 `co_return;` 或自然结束时调用**。表示协程无返回值。与 `return_value` 二选一。 |
| `return_value` | `void return_value(T val)` | **协程执行 `co_return val;` 时调用**。用于保存返回值。与 `return_void` 二选一。 |
| `yield_value` (可选) | `auto yield_value(T val)` | **协程执行 `co_yield val;` 时调用**。用于生成器模式，保存生成的值并挂起协程。 |

##### 2. Awaiter (等待体)
当代码执行 `co_await expr;` 时，`expr` 必须是一个 Awaiter（或者能转化为 Awaiter）。它控制协程是否挂起以及挂起后的行为。

**必须实现的方法：**

| 方法名 | 原型示例 | 作用 |
| :--- | :--- | :--- |
| `await_ready` | `bool await_ready()` | **检查是否需要挂起**。<br>- 返回 `true`：表示结果已就绪，**不挂起**，直接调用 `await_resume`。<br>- 返回 `false`：表示需要挂起，随后调用 `await_suspend`。 |
| `await_suspend` | `void/bool/handle await_suspend(handle h)` | **挂起时执行**。参数 `h` 是当前协程的句柄。<br>- 返回 `void`：协程保持挂起，控制权交还给调用者/恢复者。<br>- 返回 `bool`：`true` 挂起；`false` 恢复执行。<br>- 返回 `handle`：挂起当前协程，并立即恢复返回的那个协程句柄（用于对称转移）。 |
| `await_resume` | `T await_resume()` | **协程恢复后调用**。该函数的返回值就是 `co_await` 表达式的结果。 |

**标准库提供的 Awaiter：**
- `std::suspend_always`: `await_ready` 返回 `false`，总是挂起。
- `std::suspend_never`: `await_ready` 返回 `true`，从不挂起。

##### 3. std::coroutine_handle (协程句柄)
用于在外部（非协程内部）控制协程的恢复、销毁和状态查询。它是对协程状态的非拥有性引用（类似指针）。

| 方法名 | 原型 | 作用 |
| :--- | :--- | :--- |
| `resume` | `void resume()` | 恢复协程执行，直到下一个挂起点。 |
| `done` | `bool done() const` | 检查协程是否已在 `final_suspend` 处挂起（即执行完毕）。 |
| `destroy` | `void destroy()` | 显式销毁协程，释放其占用的堆内存。如果协程自动销毁（`final_suspend` 返回 `suspend_never`），则不可调用此函数。 |
| `promise` | `Promise& promise()` | 获取该句柄关联的 `promise` 对象引用。 |
| `from_promise` | `static handle from_promise(Promise& p)` | 静态方法，通过 `promise` 对象获取对应的协程句柄。 |

#### 协程关键字

- **`co_await expr`**: 尝试挂起当前协程。`expr` 必须是 Awaiter。
- **`co_yield expr`**: 等价于 `co_await promise.yield_value(expr)`。用于生成值并挂起。
- **`co_return [expr]`**: 结束协程。调用 `promise.return_value(expr)` 或 `promise.return_void()`，然后跳转到 `final_suspend`。

#### 代码示例：简单的生成器 (Generator)

这是一个简单的生成器示例，展示了如何定义 `promise_type` 和使用 `co_yield`。

```cpp
#include <iostream>
#include <coroutine>
#include <exception>

// 1. 定义协程的返回对象
struct Generator {
    // 2. 定义 promise_type，名称必须是 promise_type
    struct promise_type {
        int current_value;

        // 2.1 生成协程返回对象
        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 2.2 初始挂起：这里选择总是挂起，让用户手动开始
        std::suspend_always initial_suspend() { return {}; }

        // 2.3 最终挂起：总是挂起，确保协程结束后状态还在，以便读取可能的返回值或异常
        std::suspend_always final_suspend() noexcept { return {}; }

        // 2.4 处理异常
        void unhandled_exception() { std::terminate(); }

        // 2.5 处理 co_yield
        std::suspend_always yield_value(int value) {
            current_value = value;
            return {}; // 挂起协程
        }

        // 2.6 处理 co_return (无返回值)
        void return_void() {}
    };

    // 协程句柄
    std::coroutine_handle<promise_type> handle;

    // 构造函数
    Generator(std::coroutine_handle<promise_type> h) : handle(h) {}

    // 析构函数：负责销毁协程
    ~Generator() {
        if (handle) handle.destroy();
    }

    // 获取下一个值
    bool next() {
        handle.resume(); // 恢复协程执行
        return !handle.done(); // 如果没结束，说明 yield 了一个新值
    }

    // 获取当前值
    int value() const {
        return handle.promise().current_value;
    }
};

// 3. 编写协程函数
Generator sequence(int start, int step) {
    int val = start;
    while (val < 100) {
        co_yield val; // 挂起并返回值
        val += step;
    }
}

int main() {
    // 创建协程，此时处于 initial_suspend 挂起状态
    auto gen = sequence(0, 10);

    std::cout << "Generator started:" << std::endl;
    
    // 循环恢复协程
    while (gen.next()) {
        std::cout << "Value: " << gen.value() << std::endl;
    }

    std::cout << "Generator finished." << std::endl;
    return 0;
}
```