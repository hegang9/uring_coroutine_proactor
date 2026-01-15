#include "AsyncRead.hpp"

#include <iostream>
#include <thread>

#include "Buffer.hpp"
#include "TcpConnection.hpp"

void AsyncReadAwaitable::await_suspend(
    std::coroutine_handle<> handle) noexcept {
  // 将协程句柄保存到IoContext中，以便在读操作完成时恢复协程
  conn_->getReadContext().coro_handle = handle;
  // 提交io_uring读请求
  if (userBuf_ == nullptr) {
    conn_->submitReadRequest(nbytes_);
  } else {
    // 使用用户提供的缓冲区进行读操作
    conn_->submitReadRequestWithUserBuffer(userBuf_, userBufCap_, nbytes_);
  }
  // 返回void表示挂起协程
}

int AsyncReadAwaitable::await_resume() const noexcept {
  int n = conn_->getReadContext().result_;
  int idx = conn_->getReadContext().idx;
  if (n > 0 && idx >= 0) {
    // 使用已注册缓冲区读取数据成功，不进行拷贝，记录信息
    conn_->setCurReadBuffer(conn_->getLoop()->getRegisteredBuffer(idx));
    conn_->setCurReadBufferSize(n);
    conn_->setCurReadBufferOffset(0);
  } else if (n > 0 && idx < 0) {
    // 使用用户提供的缓冲区读取数据，同样记录缓冲区信息，以便后续对数据进行统一处理
    conn_->setCurReadBuffer(userBuf_);
    conn_->setCurReadBufferSize(n);
    conn_->setCurReadBufferOffset(0);
  } else {
    // 读取失败或者连接关闭，重置缓冲区信息后归还固定缓冲区
    conn_->setCurReadBuffer(nullptr);
    conn_->setCurReadBufferSize(0);
    conn_->setCurReadBufferOffset(0);
    if (idx >= 0) {
      conn_->getLoop()->returnRegisteredBuffer(idx);
      // 重置idx，防止重复归还
      conn_->getReadContext().idx = -1;
    }
  }
  // 返回实际读取的字节数，在后续开发中根据此结果进行错误处理
  return n;
}