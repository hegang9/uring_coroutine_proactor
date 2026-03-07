#pragma once

/**
 * @file RecommendProtocol.hpp
 * @brief 推荐服务的请求/响应协议定义
 *
 * 设计说明：
 * 在真实的搜广推系统中，请求/响应通常使用 Protobuf 或 FlatBuffers 序列化。
 * 这里为了演示框架集成，使用简单的自定义文本协议（类JSON格式），
 * 便于理解全链路流程。生产环境应替换为 Protobuf 以获得更好的序列化性能。
 *
 * 推荐服务全链路流程：
 *   客户端请求 → HTTP解析 → 召回(Recall) → 特征提取(Feature) → 排序(Rank) → 响应
 *
 * HTTP 请求格式示例：
 *   POST /recommend HTTP/1.1
 *   Content-Type: application/json
 *   Content-Length: ...
 *
 *   {"user_id": 12345, "count": 10, "scene": "homepage"}
 *
 * HTTP 响应格式示例：
 *   HTTP/1.1 200 OK
 *   Content-Type: application/json
 *
 *   {"items": [{"id":1001,"score":0.95,"reason":"猜你喜欢"}, ...], "trace_id":"abc123"}
 */

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ===================== 推荐请求结构 =====================

/**
 * @brief 推荐请求
 *
 * 模拟真实推荐系统的请求参数：
 * - user_id: 用户唯一标识，用于查询用户画像和行为特征
 * - count:   请求的推荐物品数量（Top-K）
 * - scene:   请求场景（首页推荐、详情页推荐、搜索推荐等）
 *
 * 在实际系统中，请求还可能包含：
 * - 设备信息（device_id, os, app_version）
 * - 上下文信息（当前时间、地理位置、网络类型）
 * - 已曝光列表（用于去重过滤）
 */
struct RecommendRequest
{
    uint64_t userId = 0; // 用户ID
    int count = 10;      // 请求推荐数量（Top-K）
    std::string scene;   // 请求场景标识（如 "homepage", "detail", "search"）
    std::string traceId; // 全链路追踪ID，用于日志关联和问题排查

    /**
     * @brief 从简单的JSON字符串中解析请求参数
     *
     * 注意：这是一个极简的手写解析器，仅用于演示。
     * 生产环境应使用 rapidjson / nlohmann::json / simdjson 等库。
     *
     * @param json JSON格式的请求体
     * @return 解析是否成功
     */
    bool parseFromJson(std::string_view json)
    {
        // 解析 user_id
        auto pos = json.find("\"user_id\"");
        if (pos == std::string_view::npos)
            return false;
        pos = json.find(':', pos);
        if (pos == std::string_view::npos)
            return false;
        pos++;
        // 跳过空格
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            pos++;
        userId = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        {
            userId = userId * 10 + (json[pos] - '0');
            pos++;
        }

        // 解析 count
        pos = json.find("\"count\"");
        if (pos != std::string_view::npos)
        {
            pos = json.find(':', pos);
            if (pos != std::string_view::npos)
            {
                pos++;
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
                    pos++;
                count = 0;
                while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
                {
                    count = count * 10 + (json[pos] - '0');
                    pos++;
                }
            }
        }

        // 解析 scene
        pos = json.find("\"scene\"");
        if (pos != std::string_view::npos)
        {
            pos = json.find('\"', pos + 7); // 跳过 "scene"
            if (pos != std::string_view::npos)
            {
                pos++; // 跳过开始引号
                auto end = json.find('\"', pos);
                if (end != std::string_view::npos)
                {
                    scene = std::string(json.substr(pos, end - pos));
                }
            }
        }

        return userId > 0;
    }
};

// ===================== 推荐物品结构 =====================

/**
 * @brief 推荐候选物品
 *
 * 代表一个可被推荐的物品（如商品、视频、文章等）。
 * 在搜广推系统中，一个物品会经历以下阶段：
 *   1. 召回阶段：从海量物品库（百万~亿级）中粗筛出候选集（千级）
 *   2. 粗排阶段：用轻量模型快速过滤（百级）
 *   3. 精排阶段：用复杂模型精确打分（几十级）
 *   4. 重排阶段：多样性、去重、业务规则调整
 */
struct RecommendItem
{
    uint64_t itemId = 0;      // 物品ID
    double score = 0.0;       // 推荐分数（精排模型输出）
    std::string category;     // 物品类目（如 "electronics", "food", "video"）
    std::string reason;       // 推荐理由（用于前端展示，如"猜你喜欢"、"热门推荐"）
    std::string recallSource; // 召回来源标识（如 "cf" 协同过滤、"hot" 热门、"i2i" 相似推荐）

    /**
     * @brief 将物品序列化为JSON字符串
     * @return JSON格式字符串
     */
    std::string toJson() const
    {
        std::string json = "{";
        json += "\"id\":" + std::to_string(itemId);
        json += ",\"score\":" + std::to_string(score);
        json += ",\"category\":\"" + category + "\"";
        json += ",\"reason\":\"" + reason + "\"";
        json += ",\"source\":\"" + recallSource + "\"";
        json += "}";
        return json;
    }
};

// ===================== 推荐响应结构 =====================

/**
 * @brief 推荐响应
 *
 * 包含推荐结果列表及全链路性能指标。
 * 性能指标对于搜广推系统至关重要：
 * - 用于监控各阶段耗时，发现性能瓶颈
 * - 用于A/B测试中对比不同策略的延迟表现
 * - 生产环境会上报到监控系统（如Prometheus）
 */
struct RecommendResponse
{
    std::vector<RecommendItem> items; // 推荐结果列表（已排序）
    std::string traceId;              // 全链路追踪ID

    // ---- 全链路性能指标（微秒级） ----
    int64_t recallLatencyUs = 0;  // 召回阶段耗时（微秒）
    int64_t featureLatencyUs = 0; // 特征提取阶段耗时（微秒）
    int64_t rankLatencyUs = 0;    // 排序阶段耗时（微秒）
    int64_t totalLatencyUs = 0;   // 总耗时（微秒）
    int recallCandidateCount = 0; // 召回候选数量
    int finalCount = 0;           // 最终返回数量

    /**
     * @brief 将完整响应序列化为JSON字符串
     *
     * 包含推荐结果和全链路性能耗时，方便客户端展示和调试。
     * @return JSON格式响应字符串
     */
    std::string toJson() const
    {
        std::string json = "{\"items\":[";

        // 序列化每个推荐物品
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i > 0)
                json += ",";
            json += items[i].toJson();
        }

        json += "],\"trace_id\":\"" + traceId + "\"";

        // 附加性能指标（搜广推系统的关键监控数据）
        json += ",\"latency\":{";
        json += "\"recall_us\":" + std::to_string(recallLatencyUs);
        json += ",\"feature_us\":" + std::to_string(featureLatencyUs);
        json += ",\"rank_us\":" + std::to_string(rankLatencyUs);
        json += ",\"total_us\":" + std::to_string(totalLatencyUs);
        json += "}";

        json += ",\"recall_count\":" + std::to_string(recallCandidateCount);
        json += ",\"final_count\":" + std::to_string(finalCount);
        json += "}";

        return json;
    }
};
