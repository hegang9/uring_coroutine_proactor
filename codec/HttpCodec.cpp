#include "codec/HttpCodec.hpp"

#include "Logger.hpp"
#include <algorithm>
#include <charconv>

// ===================== HttpCodec 实现 =====================

HttpCodec::DecodeResult HttpCodec::decode(Buffer *buf, HttpRequest &outRequest)
{
    // 获取当前可读数据的视图（不拷贝）
    // 注意：Buffer::readableBytes() 返回可读字节数
    // Buffer::readBeginAddr() 返回可读起始地址
    const char *data = buf->readBeginAddr();
    size_t len = buf->readableBytes();

    if (len == 0)
    {
        return DecodeResult::kIncomplete;
    }

    std::string_view sv(data, len);

    // 1. 查找请求头结束位置 "\r\n\r\n"
    size_t headerEnd = sv.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos)
    {
        // 还没收到完整的 HTTP 头（拆包）
        return DecodeResult::kIncomplete;
    }

    // 重置输出对象
    outRequest.reset();

    // 2. 解析请求行 (第一行)
    size_t firstLineEnd = sv.find("\r\n");
    std::string_view requestLine = sv.substr(0, firstLineEnd);
    if (!parseRequestLine(requestLine, outRequest))
    {
        return DecodeResult::kError;
    }

    // 3. 解析 Content-Length 和 Connection 头部
    // 简单的解析逻辑：直接查找特定字符串，而不构建完整的 header map，以提高性能

    // 解析 Content-Length
    size_t clPos = sv.find("Content-Length:");
    if (clPos == std::string_view::npos)
    {
        clPos = sv.find("content-length:");
    }

    // 确保找到的 header 在 header 部分内
    if (clPos != std::string_view::npos && clPos < headerEnd)
    {
        size_t valueStart = clPos + 15; // "Content-Length:" 长度
        // 跳过空格
        while (valueStart < headerEnd && sv[valueStart] == ' ')
            valueStart++;

        size_t valueEnd = sv.find("\r\n", valueStart);
        if (valueEnd != std::string_view::npos && valueEnd <= headerEnd)
        {
            std::string_view clValue = sv.substr(valueStart, valueEnd - valueStart);
            size_t val = 0;
            auto result = std::from_chars(clValue.data(), clValue.data() + clValue.size(), val);
            if (result.ec == std::errc())
            {
                outRequest.contentLength = val;
            }
        }
    }

    // 解析 Connection: keep-alive / close
    size_t connPos = sv.find("Connection:");
    if (connPos == std::string_view::npos)
    {
        connPos = sv.find("connection:");
    }

    if (connPos != std::string_view::npos && connPos < headerEnd)
    {
        size_t valueStart = connPos + 11;
        size_t valueEnd = sv.find("\r\n", valueStart);
        if (valueEnd != std::string_view::npos && valueEnd <= headerEnd)
        {
            std::string_view connValue = sv.substr(valueStart, valueEnd - valueStart);
            if (connValue.find("close") != std::string_view::npos)
            {
                outRequest.keepAlive = false;
            }
        }
    }

    // 4. 计算请求总长度，判断 Body 是否完整
    size_t totalLen = headerEnd + 4 + outRequest.contentLength; // 4 is "\r\n\r\n"
    if (len < totalLen)
    {
        // Body 还没收全（拆包）
        return DecodeResult::kIncomplete;
    }

    // 5. 提取 Body
    if (outRequest.contentLength > 0)
    {
        outRequest.body = std::string(sv.substr(headerEnd + 4, outRequest.contentLength));
    }

    // 6. 消耗 buffer 中的数据 (retrieve)
    // 这是关键一步，表示这部分数据已被成功消费
    buf->retrieve(totalLen);

    return DecodeResult::kComplete;
}

void HttpCodec::encode(Buffer *buf, const HttpResponse &response)
{
    // 预分配大致空间，避免多次 reallocation
    std::string respStr;
    respStr.reserve(256 + response.body.size());

    // 状态行
    respStr += "HTTP/1.1 " + std::to_string(response.statusCode) + " " + response.statusMessage + "\r\n";

    // 头部
    respStr += "Content-Type: " + response.contentType + "\r\n";
    respStr += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    respStr += response.keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";

    // 空行
    respStr += "\r\n";

    // 追加到 buffer (header + body)
    buf->append(respStr);
    buf->append(response.body);
}

bool HttpCodec::parseRequestLine(std::string_view line, HttpRequest &req)
{
    // line format: METHOD PATH VERSION
    // e.g., "GET /index.html HTTP/1.1"

    size_t methodEnd = line.find(' ');
    if (methodEnd == std::string_view::npos)
        return false;

    req.method = std::string(line.substr(0, methodEnd));

    size_t pathStart = methodEnd + 1;
    // 跳过可能的额外空格
    while (pathStart < line.size() && line[pathStart] == ' ')
        pathStart++;

    size_t pathEnd = line.find(' ', pathStart);
    if (pathEnd == std::string_view::npos)
    {
        // HTTP/0.9 简单的 GET 请求可能没有 VERSION，或者解析错误
        // 但为了健壮性，这里假设必须有 VERSION，或者就是解析失败
        // 为了宽容，如果未找到第二个空格，剩下的可能是 PATH (HTTP/0.9) 或者格式不对
        // 这里采用严格模式：必须有 Method Path Version
        return false;
    }

    req.path = std::string(line.substr(pathStart, pathEnd - pathStart));

    size_t versionStart = pathEnd + 1;
    while (versionStart < line.size() && line[versionStart] == ' ')
        versionStart++;

    req.version = std::string(line.substr(versionStart));

    return true;
}
