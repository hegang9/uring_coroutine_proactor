## liburing API 接口文档

1. io_uring_queue_init
    - 原型：int io_uring_queue_init(unsigned entries, struct io_uring *ring, unsigned flags)

    - 作用：初始化 io_uring 实例，创建提交队列（SQ）和完成队列（CQ）。

    - 参数说明：
      - entries：队列深度，建议为2的幂次方，决定了一次性能提交的最大请求数
      - ring：指向 io_uring结构体的指针
      - flags：配置标志，如 IORING_SETUP_IOPOLL（轮询模式）、IORING_SETUP_SQPOLL（SQ线程轮询）

    - 返回值：成功返回 0，失败返回负的错误码




2. io_uring_queue_init_params
    - 原型：int io_uring_queue_init_params(unsigned entries, struct io_uring *ring, struct io_uring_params *params)

    - 作用：功能同 io_uring_queue_init，但允许通过 io_uring_params结构体进行更精细的配置。

    - 参数说明：
      - entries：队列深度
      - ring：指向 io_uring结构体的指针
      - params：指向参数结构体的指针，用于详细配置队列特性

    - 返回值：成功返回 0，失败返回负的错误码




3. io_uring_queue_exit
    - 原型：void io_uring_queue_exit(struct io_uring *ring)

    - 作用：清理并释放 io_uring 实例占用的所有资源。在程序结束前必须调用。

    - 参数说明：
      - ring：要清理的 io_uring 实例指针

    - 返回值：无


4. io_uring_get_sqe
    - 原型：struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring)

    - 作用：从提交队列中获取一个空闲的提交队列条目（SQE），用于准备一个新的I/O操作。

    - 参数说明：
      - ring：io_uring 实例指针

    - 返回值：成功返回 SQE 指针，如果队列已满则返回 NULL




5. io_uring_submit
    - 原型：int io_uring_submit(struct io_uring *ring)

    - 作用：将所有已准备好的 SQE 提交给内核处理。建议批量准备多个 SQE 后一次性提交以提升效率。

    - 参数说明：
      - ring：io_uring 实例指针

    - 返回值：成功提交的 SQE 数量，或负的错误码




6. io_uring_wait_cqe
    - 原型：int io_uring_wait_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr)

    - 作用：阻塞等待，直到至少有一个完成事件（CQE）可用。

    - 参数说明：
      - ring：io_uring 实例指针
      - cqe_ptr：用于输出 CQE 指针的地址

    - 返回值：成功返回 0，失败返回负的错误码




7. io_uring_peek_cqe
    - 原型：int io_uring_peek_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr)

    - 作用：非阻塞检查完成队列中是否有可用事件。

    - 参数说明：
      - ring：io_uring 实例指针
      - cqe_ptr：用于输出 CQE 指针的地址

    - 返回值：有可用事件返回 0，无可用事件返回 -EAGAIN




8. io_uring_peek_batch_cqe
    - 原型：int io_uring_peek_batch_cqe(struct io_uring *ring, struct io_uring_cqe **cqes, int count)

    - 作用：批量获取多个可用的 CQE，有助于在高吞吐量场景下减少调用次数。

    - 参数说明：
      - ring：io_uring 实例指针
      - cqes：用于存储 CQE 指针数组的地址
      - count：想要获取的最大数量

    - 返回值：实际获取到的 CQE 数量




9. io_uring_cqe_seen
    - 原型：void io_uring_cqe_seen(struct io_uring *ring, struct io_uring_cqe *cqe)

    - 作用：标记一个 CQE 已被处理。必须调用，否则内核无法重用该 CQE 的位置。

    - 参数说明：
      - ring：io_uring 实例指针
      - cqe：已处理完的 CQE 指针

    - 返回值：无

10. io_uring_cq_advance
    - 原型：void io_uring_cq_advance(struct io_uring *ring, unsigned nr)

    - 作用：在批量处理 CQE 后，一次性标记多个 CQE 已被处理。

    - 参数说明：
      - ring：io_uring 实例指针
      - nr：要标记为已处理的 CQE 数量

    - 返回值：无

11. io_uring_prep_read
    - 原型：void io_uring_prep_read(struct io_uring_sqe *sqe, int fd, void *buf, unsigned nbytes, off_t offset)

    - 作用：准备一个异步读文件操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
    - fd：文件描述符
    - buf：数据缓冲区
    - nbytes：要读取的字节数
    - offset：文件偏移量

    - 返回值：无


12. io_uring_prep_write
    - 原型：void io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf, unsigned nbytes, off_t offset)

    - 作用：准备一个异步写文件操作。

    - 参数说明：参数同 io_uring_prep_read

    - 返回值：无


13. io_uring_prep_accept
    - 原型：void io_uring_prep_accept(struct io_uring_sqe *sqe, int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)

    - 作用：准备一个异步接受网络连接的操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - fd：监听套接字描述符
      - addr：用于存储客户端地址信息
      - addrlen：地址结构体长度
      - flags：同 accept4系统调用的 flags 参数

    - 返回值：无




14. io_uring_prep_connect
    - 原型：void io_uring_prep_connect(struct io_uring_sqe *sqe, int fd, struct sockaddr *addr, socklen_t addrlen)

    - 作用：准备一个异步连接操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - fd：套接字描述符
      - addr：目标服务器地址
      - addrlen：地址结构体长度

    - 返回值：无




15. io_uring_prep_recv
    - 原型：void io_uring_prep_recv(struct io_uring_sqe *sqe, int sockfd, void *buf, size_t len, int flags)

    - 作用：准备一个异步接收网络数据的操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - sockfd：套接字描述符
      - buf：接收缓冲区
      - len：缓冲区长度
      - flags：同 recv系统调用的 flags 参数

    - 返回值：无




16. io_uring_prep_send
    - 原型：void io_uring_prep_send(struct io_uring_sqe *sqe, int sockfd, const void *buf, size_t len, int flags)

    - 作用：准备一个异步发送网络数据的操作。

    - 参数说明：参数同 io_uring_prep_recv

    - 返回值：无




17. io_uring_prep_close
    - 原型：void io_uring_prep_close(struct io_uring_sqe *sqe, int fd)

    - 作用：准备一个异步关闭文件描述符的操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - fd：要关闭的文件描述符

    - 返回值：无




18. io_uring_prep_openat
    - 原型：void io_uring_prep_openat(struct io_uring_sqe *sqe, int dirfd, const char *path, int flags, mode_t mode)

	

    - 作用：准备一个异步打开文件的操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - dirfd：目录文件描述符
      - path：文件路径
      - flags：打开标志
      - mode：文件权限模式

    - 返回值：无




19. io_uring_prep_statx
    - 原型：void io_uring_prep_statx(struct io_uring_sqe *sqe, int dirfd, const char *path, int flags, unsigned mask, struct statx *statxbuf)

    - 作用：准备一个异步获取文件状态的操作。

    - 参数说明：
      - sqe：从 io_uring_get_sqe获取的指针
      - dirfd：目录文件描述符
      - path：文件路径
      - flags：标志位
    - mask：要获取的状态掩码
      - statxbuf：存储状态信息的缓冲区

    - 返回值：无


20. io_uring_sqe_set_data
    - 原型：void io_uring_sqe_set_data(struct io_uring_sqe *sqe, void *user_data)

    - 作用：为 SQE 设置用户自定义数据指针。该指针会在对应的 CQE 中返回，用于在异步回调中关联具体的请求上下文。

    - 参数说明：
      - sqe：提交队列条目指针
      - user_data：任意用户数据指针

    - 返回值：无




21. io_uring_cqe_get_data
    - 原型：void *io_uring_cqe_get_data(const struct io_uring_cqe *cqe)

    - 作用：从 CQE 中获取之前通过 io_uring_sqe_set_data设置的用户数据指针。

    - 参数说明：
      - cqe：完成队列条目指针

    - 返回值：之前设置的 user_data指针




22. io_uring_register_buffers
    - 原型：int io_uring_register_buffers(struct io_uring *ring, const struct iovec *iovecs, unsigned nr_iovecs)

    - 作用：提前注册固定缓冲区。可以避免每次 I/O 操作时内核进行地址映射，提升性能，属于高级优化特性。

    - 参数说明：
      - ring：io_uring 实例指针
      - iovecs：指向 iovec 结构体数组
      - nr_iovecs：数组大小

    - 返回值：成功返回 0，失败返回负的错误码




23. io_uring_unregister_buffers
    - 原型：int io_uring_unregister_buffers(struct io_uring *ring)

    - 作用：注销之前注册的缓冲区。

    - 参数说明：
      - ring：io_uring 实例指针

    - 返回值：成功返回 0，失败返回负的错误码




24. io_uring_register_files
    - 原型：int io_uring_register_files(struct io_uring *ring, const int *files, unsigned nr_files)

    - 作用：注册文件描述符，避免每次I/O操作时内核查找文件描述符的开销。

    - 参数说明：
      - ring：io_uring 实例指针
      - files：文件描述符数组
      - nr_files：数组大小

    - 返回值：成功返回 0，失败返回负的错误码




25. io_uring_unregister_files
    - 原型：int io_uring_unregister_files(struct io_uring *ring)

    - 作用：注销之前注册的文件描述符。

    - 参数说明：
      - ring：io_uring 实例指针

    - 返回值：成功返回 0，失败返回负的错误码