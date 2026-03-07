#pragma once

/**
 * @file RecommendationHandler.hpp
 * @brief 推荐服务的业务处理器
 *
 * 设计说明：
 * 这是推荐系统的"编排"层(Orchestration Layer)，负责：
 *   1. 调度各个组件（召回、特征、排序）
 *   2. 处理整个推荐流程
 *   3. 收集性能指标
 *   4. 错误处理和降级策略
 *
 * 真实的推荐系统可能包含更复杂的流程：
 *   - 多路并行召回（CF、热门、相似等）
 *   - 特征并行获取（使用异步RPC）
 *   - 粗排+精排两层排序
 *   - 多样性计算
 *   - 业务规则应用（如去重、库存检查）
 *   - 灰度和A/B测试框架
 *
 * 协程优势：
 * 在这个处理器中，如果要并行获取多个特征或调用多个服务，
 * 使用C++20协程可以简洁地表示异步流程：
 *   co_await featureService1(...)
 *   co_await featureService2(...)  // 实际系统中可能并行
 *   ... 无需回调地狱！
 */

#include "FeatureStore.hpp"
#include "RankingModel.hpp"
#include "RecommendProtocol.hpp"
#include <chrono>
#include <vector>

// ===================== 推荐处理器 =====================

class RecommendationHandler
{
  public:
    /**
     * @brief 初始化处理器
     * @param featureStore 特征存储服务
     */
    explicit RecommendationHandler(FeatureStore *featureStore) : featureStore_(featureStore)
    {
        rankingEngine_ = std::make_unique<RankingEngine>(featureStore);
    }

    /**
     * @brief 处理推荐请求的主入口
     *
     * 完整推荐流程：
     *   1. 参数校验
     *   2. 召回阶段：生成候选物品列表
     *   3. 特征阶段：为用户和物品获取特征
     *   4. 排序阶段：对候选进行排序
     *   5. 重排阶段：应用多样性等规则（这里简化掉）
     *   6. 构建响应：包含推荐结果和性能指标
     *
     * @param request 推荐请求
     * @return 推荐响应
     */
    RecommendResponse handleRecommendation(const RecommendRequest &request)
    {
        auto startTime = std::chrono::high_resolution_clock::now();
        RecommendResponse response;
        response.traceId = request.traceId;

        // ==================== 1. 参数校验 ====================
        if (request.userId == 0 || request.count <= 0 || request.count > 100)
        {
            // 参数不合法，返回空推荐
            response.finalCount = 0;
            return response;
        }

        // ==================== 2. 召回阶段 ====================
        auto recallStartTime = std::chrono::high_resolution_clock::now();

        std::vector<RecallCandidate> candidates = recall(request.userId, request.scene);

        auto recallEndTime = std::chrono::high_resolution_clock::now();
        response.recallLatencyUs =
            std::chrono::duration_cast<std::chrono::microseconds>(recallEndTime - recallStartTime).count();
        response.recallCandidateCount = static_cast<int>(candidates.size());

        if (candidates.empty())
        {
            // 召回失败，返回空推荐（真实系统应有降级方案）
            response.finalCount = 0;
            return response;
        }

        // ==================== 3. 特征阶段 ====================
        auto featureStartTime = std::chrono::high_resolution_clock::now();

        // 在这个demo中，特征获取在rank()阶段完成
        // 真实系统可能在此阶段并行获取多个特征

        auto featureEndTime = std::chrono::high_resolution_clock::now();
        response.featureLatencyUs =
            std::chrono::duration_cast<std::chrono::microseconds>(featureEndTime - featureStartTime).count();

        // ==================== 4. 排序阶段 ====================
        auto rankStartTime = std::chrono::high_resolution_clock::now();

        // 对候选进行排序，返回Top-K
        int topK = std::min(request.count, static_cast<int>(candidates.size()));
        auto rankedItems = rankingEngine_->rankCandidates(request.userId, candidates, topK);

        auto rankEndTime = std::chrono::high_resolution_clock::now();
        response.rankLatencyUs =
            std::chrono::duration_cast<std::chrono::microseconds>(rankEndTime - rankStartTime).count();

        // ==================== 5. 构建响应 ====================
        for (const auto &item : rankedItems)
        {
            RecommendItem recItem;
            recItem.itemId = item.first;
            recItem.score = item.second;
            recItem.category = getCategoryFromItemId(item.first);
            recItem.reason = getReasonFromScene(request.scene);
            response.items.push_back(recItem);
        }

        response.finalCount = static_cast<int>(response.items.size());

        auto endTime = std::chrono::high_resolution_clock::now();
        response.totalLatencyUs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

        return response;
    }

  private:
    /**
     * @brief 召回阶段：生成候选物品列表
     *
     * 在真实系统中，这里可能运行多个召回通道并行执行：
     *   1. 协同过滤召回：基于用户相似度
     *   2. 热门推荐召回：返回当前热门物品
     *   3. I2I召回：基于用户历史物品的相似推荐
     *   4. 其他召回：标签、内容等
     *
     * 这个demo简化为：返回所有物品作为候选（共100个）
     *
     * @param userId 用户ID
     * @param scene 推荐场景
     * @return 召回候选列表
     */
    std::vector<RecallCandidate> recall(uint64_t userId, const std::string &scene)
    {
        std::vector<RecallCandidate> candidates;

        // 生成100个候选物品（模拟召回结果）
        // 真实系统召回数量可能从几百到几千
        for (uint64_t itemId = 1; itemId <= 100; itemId++)
        {
            RecallCandidate candidate;
            candidate.itemId = itemId;

            // 根据itemId决定召回来源（模拟多路召回）
            if (itemId % 3 == 0)
            {
                candidate.recallSource = "cf"; // 协同过滤
                candidate.recallScore = 0.8;
            }
            else if (itemId % 3 == 1)
            {
                candidate.recallSource = "i2i"; // I2I相似
                candidate.recallScore = 0.6;
            }
            else
            {
                candidate.recallSource = "hot"; // 热门推荐
                candidate.recallScore = 0.4;
            }

            candidates.push_back(candidate);
        }

        return candidates;
    }

    /**
     * @brief 根据场景获取推荐理由
     *
     * 用户界面展示推荐理由，提升推荐透明度和用户体验。
     * 常见的理由有：
     *   - "猜你喜欢"：个性化推荐
     *   - "热门推荐"：热度驱动
     *   - "看过的人也买了"：协同过滤
     *
     * @param scene 推荐场景
     * @return 推荐理由文本
     */
    std::string getReasonFromScene(const std::string &scene) const
    {
        if (scene == "homepage")
            return "猜你喜欢";
        else if (scene == "detail")
            return "看过的人也买了";
        else if (scene == "search")
            return "热门推荐";
        else
            return "推荐给你";
    }

    /**
     * @brief 根据物品ID获取物品分类
     *
     * 模拟物品分类映射。真实系统中通常从商品数据库或特征库获取。
     *
     * @param itemId 物品ID
     * @return 物品分类标签
     */
    std::string getCategoryFromItemId(uint64_t itemId) const
    {
        // 简单的模拟：根据ID取模映射到几个分类
        int categoryIdx = itemId % 5;
        switch (categoryIdx)
        {
        case 0:
            return "electronics";
        case 1:
            return "books";
        case 2:
            return "clothing";
        case 3:
            return "home";
        default:
            return "others";
        }
    }

    FeatureStore *featureStore_;                   // 特征存储服务
    std::unique_ptr<RankingEngine> rankingEngine_; // 排序引擎
};
