# liburing 接口说明

`liburing` 是 Linux 内核 `io_uring` 异步 I/O 接口的用户空间封装库，提供了简单易用的 API 来操作 `io_uring`。

## 1. io_uring_queue_init

### 原型
```c
int io_uring_queue_init(unsigned entries, struct io_uring *ring, unsigned flags);
```

### 作用
初始化 `io_uring` 实例，设置提交队列 (SQ) 和完成队列 (CQ)。

### 参数说明
*   `entries`: 队列的深度（大小），必须是 2 的幂次（如 4096）。
*   `ring`: 指向 `struct io_uring` 结构体的指针，用于保存初始化的实例信息。
*   `flags`: 标志位，用于控制 `io_uring` 的行为。常见标志：
    *   `0`: 默认行为。
    *   `IORING_SETUP_SQPOLL`: 启用内核线程轮询提交队列，减少系统调用开销。

### 返回值说明
*   成功：返回 `0`。
*   失败：返回负的错误码（如 `-errno`）。

### 注意事项
*   在使用完 `ring` 后，必须调用 `io_uring_queue_exit` 释放资源。

---

## 2. io_uring_get_sqe

### 原型
```c
struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring);
```

### 作用
从提交队列 (SQ) 中获取一个空闲的提交队列条目 (SQE)，用于准备新的 I/O 请求。

### 参数说明
*   `ring`: 指向已初始化的 `io_uring` 实例。

### 返回值说明
*   成功：返回指向 `struct io_uring_sqe` 的指针。
*   失败：如果 SQ 已满，返回 `NULL`。

### 注意事项
*   获取 SQE 后，需要使用 `io_uring_prep_*` 系列函数填充它。
*   如果返回 `NULL`，通常意味着需要先提交 (`io_uring_submit`) 现有请求以腾出空间。

---

## 3. io_uring_prep_* 系列函数

这类函数用于填充 SQE，指定具体的 I/O 操作。

### 示例：io_uring_prep_readv

### 原型
```c
void io_uring_prep_readv(struct io_uring_sqe *sqe, int fd, const struct iovec *iovecs, unsigned nr_vecs, __u64 offset);
```

### 作用
准备一个异步读取请求（类似于 `preadv`）。

### 参数说明
*   `sqe`: 通过 `io_uring_get_sqe` 获取的 SQE 指针。
*   `fd`: 文件描述符。
*   `iovecs`: 指向 `iovec` 数组的指针，描述接收数据的缓冲区。
*   `nr_vecs`: `iovec` 数组的长度。
*   `offset`: 文件偏移量。

### 其他常用 Prep 函数
*   `io_uring_prep_writev`: 异步写。
*   `io_uring_prep_accept`: 异步接受连接。
*   `io_uring_prep_connect`: 异步连接。
*   `io_uring_prep_poll_add`: 异步轮询（类似 epoll）。
*   `io_uring_prep_nop`: 空操作，常用于测试。

### 注意事项
*   `io_uring_sqe_set_data(sqe, user_data)`: 这是一个非常重要的辅助宏/函数，用于将用户自定义数据（如指针）绑定到 SQE。当请求完成时，CQE 中会包含这个 `user_data`，用于识别是哪个请求完成了。

---

## 4. io_uring_submit

### 原型
```c
int io_uring_submit(struct io_uring *ring);
```

### 作用
将填充好的 SQE 提交给内核进行处理。

### 参数说明
*   `ring`: 指向 `io_uring` 实例。

### 返回值说明
*   成功：返回提交的 SQE 数量。
*   失败：返回负的错误码。

### 注意事项
*   调用此函数会触发系统调用（除非使用了 `IORING_SETUP_SQPOLL`）。

---

## 5. io_uring_wait_cqe

### 原型
```c
int io_uring_wait_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr);
```

### 作用
等待至少一个完成队列条目 (CQE) 到达。如果 CQ 为空，调用线程会阻塞。

### 参数说明
*   `ring`: 指向 `io_uring` 实例。
*   `cqe_ptr`: 指向指针的指针，用于接收指向 CQE 的指针。

### 返回值说明
*   成功：返回 `0`。
*   失败：返回负的错误码。

### 变体
*   `io_uring_peek_cqe`: 非阻塞检查 CQE。

---

## 6. io_uring_cqe_seen

### 原型
```c
void io_uring_cqe_seen(struct io_uring *ring, struct io_uring_cqe *cqe);
```

### 作用
标记一个 CQE 已经被处理完毕，内核可以重用该 CQE 的槽位。

### 参数说明
*   `ring`: 指向 `io_uring` 实例。
*   `cqe`: 指向要标记的 CQE。

### 注意事项
*   处理完 CQE 后必须调用此函数，否则 CQ 队列会满，导致无法接收新的完成事件。

---

## 7. io_uring_queue_exit

### 原型
```c
void io_uring_queue_exit(struct io_uring *ring);
```

### 作用
销毁 `io_uring` 实例，释放相关资源（如内存映射）。

### 参数说明
*   `ring`: 指向 `io_uring` 实例。

---

## 用法示例代码

以下是一个简单的使用 `liburing` 读取文件的示例：

```c
#include <liburing.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_DEPTH 4
#define BLOCK_SZ    1024

int main() {
    struct io_uring ring;
    int i, fd, ret;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct iovec iov;
    char buff[BLOCK_SZ];
    off_t offset = 0;

    // 1. 初始化 io_uring
    ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        return 1;
    }

    // 打开文件
    fd = open("test.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 2. 获取 SQE
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fprintf(stderr, "Could not get SQE.\n");
        return 1;
    }

    // 准备缓冲区
    iov.iov_base = buff;
    iov.iov_len = BLOCK_SZ;

    // 3. 准备 Readv 请求
    io_uring_prep_readv(sqe, fd, &iov, 1, offset);
    
    // 设置用户数据 (可选，用于在 CQE 中识别请求)
    io_uring_sqe_set_data(sqe, (void*)1);

    // 4. 提交请求
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        fprintf(stderr, "submit: %s\n", strerror(-ret));
        return 1;
    }

    // 5. 等待完成
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "wait_cqe: %s\n", strerror(-ret));
        return 1;
    }

    // 处理结果
    if (cqe->res < 0) {
        fprintf(stderr, "Async read failed: %s\n", strerror(-cqe->res));
    } else {
        printf("Read %d bytes: %.*s\n", cqe->res, cqe->res, buff);
    }

    // 6. 标记 CQE 已处理
    io_uring_cqe_seen(&ring, cqe);

    // 7. 清理
    close(fd);
    io_uring_queue_exit(&ring);

    return 0;
}
```
