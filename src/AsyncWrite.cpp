#include "AsyncWrite.hpp"

#include <iostream>
#include <thread>

#include "Buffer.hpp"
#include "TcpConnection.hpp"

void AsyncWriteAwaitable::await_suspend(
    std::coroutine_handle<> handle) noexcept {
  // 将协程句柄保存到IoContext中，以便在写操作完成时恢复协程
  conn_->getWriteContext().coro_handle = handle;

  if (regBuf_ != nullptr) {
    // 零拷贝模式：使用已注册缓冲区发送数据
    conn_->submitWriteRequestWithRegBuffer(regBuf_, regBufLen_, regBufIdx_);
  } else {
    // 普通模式：从 outputBuffer_ 发送数据
    conn_->submitWriteRequest();
  }

  // 返回void表示挂起协程
}

int AsyncWriteAwaitable::await_resume() const noexcept {
  // 返回实际写入的字节数，在后续开发中根据此结果进行错误处理
  int n = conn_->getWriteContext().result_;

  if (regBuf_ != nullptr) {
    // 零拷贝模式：写完后归还已注册缓冲区
    // 注意：归还操作在 TcpConnection::releaseCurReadBuffer() 中进行，
    // 因为读写可能使用同一个缓冲区
  } else if (n > 0) {
    conn_->getOutputBuffer().retrieve(n);  // 从输出缓冲区移除已写入的数据
  }
  return n;
}