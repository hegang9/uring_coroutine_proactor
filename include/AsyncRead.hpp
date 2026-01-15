#pragma once
#include <coroutine>
#include <cstddef>

#include "MemoryPool.hpp"

class TcpConnection;

class AsyncReadAwaitable {
 public:
  // 禁用拷贝和赋值
  AsyncReadAwaitable(const AsyncReadAwaitable&) = delete;
  AsyncReadAwaitable& operator=(const AsyncReadAwaitable&) = delete;
  // 零拷贝版本，数据读入固定缓冲区
  AsyncReadAwaitable(TcpConnection* conn, std::size_t nbytes)
      : conn_(conn), nbytes_(nbytes), userBuf_{nullptr}, userBufCap_{0} {}
  // 重载构造函数，支持用户提供缓冲区，将数据读入用户缓冲区
  AsyncReadAwaitable(TcpConnection* conn, char* userBuf, std::size_t userBufCap,
                     std::size_t nbytes)
      : conn_(conn),
        nbytes_(nbytes),
        userBuf_(userBuf),
        userBufCap_(userBufCap) {}
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept;
  int await_resume() const noexcept;
  ~AsyncReadAwaitable() = default;

  // 重载new/delete，接入内存池
  static void* operator new(size_t size) { return HashBucket::useMemory(size); }
  static void operator delete(void* p, size_t size) {
    HashBucket::freeMemory(p, size);
  }

 private:
  TcpConnection* conn_;
  std::size_t nbytes_;
  char* userBuf_;
  std::size_t userBufCap_;
};