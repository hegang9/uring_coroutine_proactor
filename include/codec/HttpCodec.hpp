#pragma once

#include "Buffer.hpp"
#include <optional>
#include <string>
#include <string_view>

// ===================== HTTP 数据结构定义 =====================

/**
 * @brief 简单的 HTTP 请求结构
 */
struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::string body;

    // 简化的头部存储，实际项目可能需要 map<string, string>
    // 这里提取关键字段方便业务使用
    size_t contentLength = 0;
    bool keepAlive = true;

    // 重置状态，用于复用对象
    void reset()
    {
        method.clear();
        path.clear();
        version.clear();
        body.clear();
        contentLength = 0;
        keepAlive = true;
    }
};

/**
 * @brief 简单的 HTTP 响应结构
 */
struct HttpResponse
{
    int statusCode = 200;
    std::string statusMessage = "OK";
    std::string contentType = "text/plain";
    std::string body;
    bool keepAlive = true;

    // 设置包体并自动设置 Content-Length
    void setBody(const std::string &b)
    {
        body = b;
    }

    // 便捷构造函数
    HttpResponse(int code = 200, std::string msg = "OK", bool ka = true)
        : statusCode(code), statusMessage(msg), keepAlive(ka)
    {
    }
};

// ===================== Codec 接口定义 =====================

/**
 * @brief HTTP 编解码器
 * 负责解决 TCP 粘包/拆包问题，并在 Buffer 和 HttpRequest/HttpResponse 对象之间转换
 */
class HttpCodec
{
  public:
    // 解析结果枚举
    enum class DecodeResult
    {
        kComplete,   // 解析出一个完整包
        kIncomplete, // 数据不足（拆包），需要更多数据
        kError       // 解析错误（协议格式错误）
    };

    /**
     * @brief 尝试从 buffer 中解析出一个 HTTP 请求
     *
     * 如果解析成功 (kComplete)，会消耗 buffer 中相应的数据，并填充 outRequest。
     * 如果数据不足 (kIncomplete)，不会消耗 buffer 数据。
     * 如果出错 (kError)，可能会消耗部分数据或不消耗。
     *
     * @param buf 输入缓冲区
     * @param outRequest 输出参数，解析成功时填充
     * @return DecodeResult
     */
    static DecodeResult decode(Buffer *buf, HttpRequest &outRequest);

    /**
     * @brief 将 HTTP 响应编码并追加到 buffer 中
     *
     * @param buf 输出缓冲区
     * @param response 待发送的响应对象
     */
    static void encode(Buffer *buf, const HttpResponse &response);

  private:
    // 内部解析辅助函数
    static bool parseRequestLine(std::string_view line, HttpRequest &req);
};
