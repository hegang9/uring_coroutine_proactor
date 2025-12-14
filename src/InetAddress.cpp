#include "InetAddress.hpp"
#include <string>

InetAddress::InetAddress(uint16_t port, const std::string &ip)
{
    addr_.sin_family = AF_INET;   // IPv4
    addr_.sin_port = htons(port); // 转换为网络字节序
    // addr_.sin_addr.s_addr = inet_addr(ip.c_str()); 旧接口
    inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr); // 将字符串IP地址转换为二进制形式
}

std::string InetAddress::toIpPort() const
{
    char buf[64] = {0};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf)); // 将二进制IP地址转换为字符串形式
    /*
    size_t ip_end=::strlen(buf);
    uint16_t port=ntohs(addr_.sin_port);
    snprintf(buf+ip_end, sizeof(buf)-ip_end, ":%u", port);  // 将端口转换为字符串，拼接端口号，写入到buf中
    return std::string(buf);
    */
    return std::string(buf) + ":" + std::to_string(::ntohs(addr_.sin_port)); // ntohs将网络字节序转换为主机字节序，大端转小端
}
