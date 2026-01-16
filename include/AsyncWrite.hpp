#pragma once
#include <coroutine>
#include <cstddef>

#include "MemoryPool.hpp"

class TcpConnection;

class AsyncWriteAwaitable {
 public:
  // 禁用拷贝和赋值
  AsyncWriteAwaitable(const AsyncWriteAwaitable&) = delete;
  AsyncWriteAwaitable& operator=(const AsyncWriteAwaitable&) = delete;
  // 普通写操作，从 outputBuffer_ 发送数据
  AsyncWriteAwaitable(TcpConnection* conn)
      : conn_(conn), regBuf_(nullptr), regBufLen_(0), regBufIdx_(-1) {}
  // 零拷贝写操作，直接从已注册缓冲区发送数据
  AsyncWriteAwaitable(TcpConnection* conn, void* regBuf, size_t len, int idx)
      : conn_(conn), regBuf_(regBuf), regBufLen_(len), regBufIdx_(idx) {}
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept;
  int await_resume() const noexcept;
  ~AsyncWriteAwaitable() = default;

  // 重载new/delete，接入内存池
  static void* operator new(size_t size) { return HashBucket::useMemory(size); }
  static void operator delete(void* p, size_t size) {
    HashBucket::freeMemory(p, size);
  }

 private:
  TcpConnection* conn_;  // 关联的TcpConnection对象
  void* regBuf_;         // 已注册缓冲区指针（零拷贝模式）
  size_t regBufLen_;     // 已注册缓冲区数据长度
  int regBufIdx_;        // 已注册缓冲区索引（用于写完后归还）
};