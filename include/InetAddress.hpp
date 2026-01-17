#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string>

/**
 * 封装网络地址相关操作的类。
struct sockaddr_in {
    sa_family_t    sin_family; // 地址族，如 AF_INET
    in_port_t      sin_port;   // 端口号（网络字节序，使用 htons() 转换）
    struct in_addr sin_addr;   // IP 地址
};
struct in_addr {
    uint32_t s_addr;           // 地址（网络字节序，使用 inet_addr设置）
};
 */
class InetAddress {
 private:
  struct sockaddr_in addr_;

 public:
  explicit InetAddress(
      uint16_t port = 0,
      const std::string& ip =
          "0.0.0.0");  // 默认构造函数，ip默认是本机地址，端口号默认是0
  explicit InetAddress(const sockaddr_in& addr)
      : addr_(addr) {}  // 通过已有的sockaddr_in结构体构造SockAddress对象

  // 将IP地址和端口号从网络字节序的二进制整数形式转为人类可读的字符串形式，便于调试和日志记录
  std::string toIpPort() const;

  // 获取底层的sockaddr_in结构体引用，用于系统调用
  const sockaddr_in& getSockAddrIn() const { return addr_; }
  // 将内核系统调用accept接受连接的对端地址保存，用于保存对端客户的地址信息（系统调用不会保存对端地址），为后续日志系统和网络安全管理做准备，最终这个对端地址应该保存在TcpConnection对象中
  void setSockAddr(const sockaddr_in& addr) { addr_ = addr; }
};