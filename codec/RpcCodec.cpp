#include "codec/RpcCodec.hpp"
#include "Logger.hpp"
#include <arpa/inet.h> // for htonl, ntohl
#include <cstring>
#include <endian.h> // for be64toh, htobe64 (Linux specific)

// 辅助函数：处理 64 位整数的大端序转换
// 如果是非 Linux 环境，可能需要改为 standard byteswap functions
static uint64_t hostToNetwork64(uint64_t host64)
{
    return htobe64(host64);
}

static uint64_t networkToHost64(uint64_t net64)
{
    return be64toh(net64);
}

RpcCodec::DecodeResult RpcCodec::decode(Buffer *buf, RpcMessage &outMsg)
{
    // 头部格式:
    // 0-3:   Body Length (4 bytes, Network Byte Order)
    // 4-7:   Type        (4 bytes, Network Byte Order)
    // 8-15:  ID          (8 bytes, Network Byte Order)
    // 16...: Body Payload

    if (buf->readableBytes() < kHeaderLength)
    {
        return DecodeResult::kIncomplete; // 连头部都不够
    }

    const char *data = buf->readBeginAddr();

    // 1. 读取 Body 长度
    uint32_t bodyLenNet = 0;
    std::memcpy(&bodyLenNet, data, sizeof(uint32_t));
    uint32_t bodyLen = ntohl(bodyLenNet);

    // 2. 检查长度是否合法
    if (bodyLen > kMaxMessageLength)
    {
        LOG_ERROR("RpcCodec: Message too large, len={}", bodyLen);
        return DecodeResult::kError;
    }

    // 3. 检查数据是否完整 (Header + Body)
    // 注意：这里的 Length 是指 Body 的长度，Total Length = kHeaderLength + Body Length
    // 但我们的协议定义 header 里存储的是 Body 的长度，
    // 因此我们需要读 kHeaderLength 及其后面的 bodyLen
    // 实际上我们在 Header 里读到的 Length 通常有两种定义：
    // A. 整个包的长度 (Header + Body)
    // B. 仅 Body 的长度
    // 这里我们采用方案 B：Length 字段仅表示后续 Body 的长度。
    // 但是，为了方便，我们把 Type 和 ID 也算作 Header 的一部分。
    // 所以这里的协议实际上是：
    // [BodyLength(4)] [Type(4)] [ID(8)] [Payload(N)]
    // 其中 BodyLength = Payload 的长度 (N)
    // 而不是 (4+8+N)。

    // 也就是：
    // Total Size Needed = 4 (BodyLenField) + 4 (Type) + 8 (ID) + BodyLen

    // 修正一下上面的 kHeaderLength 定义，我们把它视为固定前缀部分
    // 前缀部分长度 = 4 (Len) + 4 (Type) + 8 (ID) = 16 字节

    size_t totalLen = kHeaderLength + bodyLen;

    if (buf->readableBytes() < totalLen)
    {
        return DecodeResult::kIncomplete; // Body 不完整
    }

    // 4. 解析头部剩余部分
    uint32_t typeNet = 0;
    std::memcpy(&typeNet, data + 4, sizeof(uint32_t));
    outMsg.type = ntohl(typeNet);

    uint64_t idNet = 0;
    std::memcpy(&idNet, data + 8, sizeof(uint64_t));
    outMsg.id = networkToHost64(idNet);

    // 5. 提取 Body Payload
    if (bodyLen > 0)
    {
        outMsg.payload.assign(data + kHeaderLength, bodyLen);
    }
    else
    {
        outMsg.payload.clear();
    }

    // 6. 消耗 buffer
    buf->retrieve(totalLen);

    return DecodeResult::kComplete;
}

void RpcCodec::encode(Buffer *buf, const RpcMessage &msg)
{
    // 计算包体长度
    uint32_t bodyLen = static_cast<uint32_t>(msg.payload.size());
    size_t totalLen = kHeaderLength + bodyLen;

    // 预分配空间
    buf->ensureWritableBytes(totalLen);

    char *data = buf->writeBeginAddr();

    // 1. 写入 Body Length (4 bytes)
    uint32_t bodyLenNet = htonl(bodyLen);
    std::memcpy(data, &bodyLenNet, sizeof(uint32_t));

    // 2. 写入 Type (4 bytes)
    uint32_t typeNet = htonl(static_cast<uint32_t>(msg.type));
    std::memcpy(data + 4, &typeNet, sizeof(uint32_t));

    // 3. 写入 ID (8 bytes)
    uint64_t idNet = hostToNetwork64(msg.id);
    std::memcpy(data + 8, &idNet, sizeof(uint64_t));

    // 4. 写入 Body Payload
    if (bodyLen > 0)
    {
        std::memcpy(data + kHeaderLength, msg.payload.data(), bodyLen);
    }

    // 5. 更新 buffer 写索引
    buf->hasWritten(totalLen);
}
