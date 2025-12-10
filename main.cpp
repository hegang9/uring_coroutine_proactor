#include <liburing.h>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

int main() {
    struct io_uring ring;
    int ret;

    // 初始化 io_uring
    ret = io_uring_queue_init(8, &ring, 0);
    if (ret < 0) {
        std::cerr << "io_uring_queue_init failed: " << -ret << std::endl;
        return 1;
    }

    std::cout << "io_uring initialized successfully!" << std::endl;

    // 退出 io_uring
    io_uring_queue_exit(&ring);
    return 0;
}
