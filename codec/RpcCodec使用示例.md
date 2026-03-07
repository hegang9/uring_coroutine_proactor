```c++
#include "codec/RpcCodec.hpp"

// 在业务协程中
while (true) {
    // 1. 读数据
    int n = co_await conn->asyncRead(4096);
    if (n <= 0) break;

    // 2. 解包循环
    while (conn->getInputBuffer()->readableBytes() > 0) {
        RpcMessage msg;
        // 尝试解码
        auto res = RpcCodec::decode(conn->getInputBuffer(), msg);
        
        if (res == RpcCodec::DecodeResult::kIncomplete) {
            break; // 数据不够，等下一次 asyncRead
        }
        
        // 3. 处理业务
        if (msg.type == 0) { // Request
            // ... 处理请求 ...
            
            // 4. 回复响应
            RpcMessage resp;
            resp.type = 1; // Response
            resp.id = msg.id; // 对应 ID
            resp.payload = "Pong";
            
            RpcCodec::encode(conn->getOutputBuffer(), resp);
            co_await conn->asyncSend("");
        }
    }
}
```