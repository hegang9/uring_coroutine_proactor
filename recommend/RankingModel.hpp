#pragma once

/**
 * @file RankingModel.hpp
 * @brief 推荐系统的精排模型
 *
 * 设计说明：
 * 在搜广推系统中，排序阶段通常包含多个层次：
 *
 *   1. 粗排(Coarse Rank)：
 *      - 使用轻量级模型（如LR、简单神经网络）
 *      - 延迟要求严格（<10ms）
 *      - 目标：从千级候选压减到百级
 *
 *   2. 精排(Fine Rank)：
 *      - 使用复杂模型（如深度神经网络、梯度提升树）
 *      - 延迟要求中等（<50ms）
 *      - 目标：从百级候选精确排序
 *
 *   3. 重排(Re-Rank)：
 *      - 应用多样性、去重、业务规则等
 *      - 可能包含强化学习、多目标优化
 *      - 通常在精排之后
 *
 * 此处实现一个简化的精排模型：
 *   - 基础分数：用户-物品特征向量的余弦相似度
 *   - 多路融合：融合多个信号（热度、交互历史等）
 *   - 线性加权：简单的线性融合
 *
 * 真实的精排模型可能使用：
 *   - Wide & Deep Learning
 *   - DeepFM (Deep Factorization Machines)
 *   - TensorFlow Serving / ONNX Runtime 部署
 *   - GPU加速推理 (TensorRT)
 */

#include "FeatureStore.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ===================== 召回候选结构 =====================

/**
 * @brief 召回阶段的候选物品
 *
 * 召回(Recall)的目标是从海量物品库中快速粗筛：
 *   - 协同过滤(CF)：基于用户相似度或物品相似度
 *   - 热门推荐(Hot)：返回当前热门物品
 *   - 个性化推荐(I2I)：基于用户历史物品的相似推荐
 *   - 多路召回：并行运行多个召回通道，最后融合
 *
 * 在这个demo中，我们简单地返回所有物品作为"召回"结果。
 * 真实系统会更复杂，可能涉及：
 *   - 倒排索引
 *   - 向量检索（如Faiss、Milvus）
 *   - 图数据库（Neo4j、ArangoDB）
 */
struct RecallCandidate
{
    uint64_t itemId = 0;      // 物品ID
    std::string recallSource; // 召回来源（"cf", "hot", "i2i"等）
    double recallScore = 0.0; // 召回阶段的初步分数
};

// ===================== 排序上下文 =====================

/**
 * @brief 排序计算的上下文
 *
 * 包含排序所需的所有输入信息，以及中间计算结果。
 */
struct RankingContext
{
    uint64_t userId = 0;
    uint64_t itemId = 0;

    // 特征信息
    FeatureVector userFeature;
    FeatureVector itemFeature;

    // 从召回得到的初步信息
    std::string recallSource;
    double recallScore = 0.0;

    // 物品热度特征（0-100，模拟）
    int itemPopularity = 50;

    // 排序阶段的计算结果
    double similarityScore = 0.0; // 基于特征相似度的分数
    double baseScore = 0.0;       // 基础综合分数
    double finalScore = 0.0;      // 最终分数（应用额外调整后）
};

// ===================== 排序模型 =====================

/**
 * @brief 推荐排序模型
 *
 * 核心功能：
 *   1. 特征交叉(Feature Crossing)：组合用户和物品特征
 *   2. 模型推理(Model Inference)：计算预测分数
 *   3. 多路融合(Multi-signal Fusion)：融合多个信号
 *   4. 动态权重调整(Dynamic Weighting)：支持A/B测试
 *
 * 性能考虑：
 *   - 向量操作使用简单计算（无SIMD优化）
 *   - 真实系统应使用BLAS库或GPU计算
 *   - 目标延迟：<5ms/候选物品（精排对数百个物品）
 */
class RankingModel
{
  public:
    /**
     * @brief 初始化排序模型
     *
     * 模型的权重参数（在实际应用中来自ML训练）
     */
    RankingModel()
    {
        // 特征相似度权重（推荐个性化的关键）
        weights_.similarityWeight = 0.4f;

        // 热度信号权重（冷启动物品的辅助）
        weights_.popularityWeight = 0.2f;

        // 召回源信号权重（不同召回路的可信度不同）
        weights_.recallSourceWeight = 0.2f;

        // 初始召回分数权重
        weights_.recallScoreWeight = 0.2f;
    }

    /**
     * @struct ModelWeights
     * @brief 模型权重参数
     *
     * 这些权重通常来自离线训练（用样本数据拟合）
     * 实际系统可能有数百个权重参数
     */
    struct ModelWeights
    {
        float similarityWeight = 0.4f;   // 特征相似度权重
        float popularityWeight = 0.2f;   // 热度权重
        float recallSourceWeight = 0.2f; // 召回源权重
        float recallScoreWeight = 0.2f;  // 初始召回分数权重

        // 权重应该归一化
        float getTotalWeight() const
        {
            return similarityWeight + popularityWeight + recallSourceWeight + recallScoreWeight;
        }
    };

    /**
     * @brief 计算物品的排序分数
     *
     * 排序公式（简化版）：
     *   score = w1 * sim(user_feature, item_feature)
     *         + w2 * normalize(item_popularity)
     *         + w3 * source_score(recall_source)
     *         + w4 * recall_score
     *
     * 其中：
     *   - sim() 是余弦相似度（0-1）
     *   - normalize() 将热度从0-100映射到0-1
     *   - source_score() 根据召回源返回信任度（0-1）
     *   - 各项分数都在[0,1]之间，加权求和得到最终分数
     *
     * @param ctx 排序上下文（包含特征和召回信息）
     * @return 最终排序分数（0-1）
     */
    double rank(RankingContext &ctx)
    {
        // 1. 计算特征相似度（0-1）
        // 余弦相似度范围[-1, 1]，这里规范化到[0, 1]
        ctx.similarityScore = ctx.userFeature.cosineSimilarity(ctx.itemFeature);
        ctx.similarityScore = (ctx.similarityScore + 1.0) / 2.0; // 映射到[0, 1]

        // 2. 热度信号（0-1）
        // 假设热度范围[0-100]，简单归一化
        double popularityScore = ctx.itemPopularity / 100.0;

        // 3. 召回源信号（0-1）
        // 不同的召回路可信度不同
        double sourceScore = getSourceScore(ctx.recallSource);

        // 4. 初始召回分数（已经在0-1范围）
        double recallScore = std::max(0.0, std::min(1.0, ctx.recallScore));

        // 5. 加权融合
        // 归一化权重（确保总权重=1）
        float totalWeight = weights_.getTotalWeight();
        float w1 = weights_.similarityWeight / totalWeight;
        float w2 = weights_.popularityWeight / totalWeight;
        float w3 = weights_.recallSourceWeight / totalWeight;
        float w4 = weights_.recallScoreWeight / totalWeight;

        ctx.baseScore = w1 * ctx.similarityScore + w2 * popularityScore + w3 * sourceScore + w4 * recallScore;

        // 6. 应用额外的调整（如点击率CTR预估、转化率CVR预估等）
        // 这里简单地应用一个非线性变换（模拟排序模型的输出层）
        ctx.finalScore = applySoftmax(ctx.baseScore);

        return ctx.finalScore;
    }

    /**
     * @brief 获取模型权重
     */
    const ModelWeights &getWeights() const
    {
        return weights_;
    }

    /**
     * @brief 更新模型权重（支持A/B测试或在线学习）
     */
    void setWeights(const ModelWeights &weights)
    {
        weights_ = weights;
    }

  private:
    /**
     * @brief 根据召回源获取信信度分数
     *
     * 不同的召回策略产出的候选质量不同：
     *   - CF(协同过滤)：高精准度，适合有交互历史的用户
     *   - I2I(相似推荐)：中等精准度
     *   - Hot(热门)：低精准度，但可用于冷启动
     *
     * @param source 召回来源标识
     * @return 信任度得分（0-1）
     */
    double getSourceScore(const std::string &source) const
    {
        if (source == "cf")
            return 0.9; // 协同过滤信任度最高
        else if (source == "i2i")
            return 0.7; // 相似推荐
        else if (source == "hot")
            return 0.5; // 热门推荐信任度较低
        else
            return 0.5; // 未知来源的默认值
    }

    /**
     * @brief 应用Softmax风格的归一化
     *
     * 将分数[0,1]映射到更陡峭的曲线，放大差异。
     * 公式：sigmoid(x) = 1 / (1 + exp(-k*x))
     * 其中k是陡峭度参数。
     *
     * 用途：增加排序模型对特征差异的敏感度
     */
    double applySoftmax(double score) const
    {
        // 简单的sigmoid函数，参数k控制陡峭度
        const double k = 2.0; // 陡峭度
        return 1.0 / (1.0 + std::exp(-k * (2 * score - 1.0)));
    }

    ModelWeights weights_; // 模型权重参数
};

// ===================== 排序引擎 =====================

/**
 * @brief 推荐排序引擎
 *
 * 整合召回候选、特征提取、排序计算、最终排序。
 * 在真实系统中，这个模块会：
 *   1. 接收多个召回路的候选列表
 *   2. 并行提取候选的特征（可能涉及多次RPC调用）
 *   3. 批量送入排序模型进行推理
 *   4. 按分数排序并返回Top-K
 *   5. 应用最终的多样性算法和业务规则
 *
 * 性能优化：
 *   - 批处理(Batching)：一次推理多个样本
 *   - 特征预热(Prefetch)：提前加载热点特征
 *   - 并行化：多线程或GPU推理
 */
class RankingEngine
{
  public:
    /**
     * @brief 初始化排序引擎
     * @param featureStore 特征存储服务
     */
    explicit RankingEngine(FeatureStore *featureStore) : featureStore_(featureStore)
    {
    }

    /**
     * @brief 对召回候选进行排序
     *
     * 主要流程：
     *   1. 获取用户特征
     *   2. 对每个候选物品：
     *      a. 获取物品特征
     *      b. 计算排序分数
     *   3. 按分数降序排序
     *   4. 返回Top-K
     *
     * @param userId 用户ID
     * @param candidates 召回候选列表
     * @param topK 最终返回的物品数量
     * @return 排序后的物品分数对（itemId, score）
     */
    std::vector<std::pair<uint64_t, double>> rankCandidates(uint64_t userId,
                                                            const std::vector<RecallCandidate> &candidates, int topK)
    {
        std::vector<std::pair<uint64_t, double>> results;

        // 特殊处理：如果候选为空，直接返回
        if (candidates.empty())
            return results;

        // 限制topK不超过候选数量
        topK = std::min(topK, static_cast<int>(candidates.size()));

        // 获取用户特征（只需一次）
        FeatureVector userFeature = featureStore_->getUserFeature(userId);

        // 对每个候选物品计算排序分数
        std::vector<std::pair<uint64_t, double>> itemScores;
        itemScores.reserve(candidates.size());

        for (const auto &candidate : candidates)
        {
            RankingContext ctx;
            ctx.userId = userId;
            ctx.itemId = candidate.itemId;
            ctx.userFeature = userFeature;
            ctx.itemFeature = featureStore_->getItemFeature(candidate.itemId);
            ctx.recallSource = candidate.recallSource;
            ctx.recallScore = candidate.recallScore;
            // 模拟热度：基于物品ID生成伪随机热度
            ctx.itemPopularity = 30 + (candidate.itemId % 50);

            // 调用排序模型计算分数
            double score = rankingModel_.rank(ctx);
            itemScores.emplace_back(candidate.itemId, score);
        }

        // 按分数降序排序
        std::sort(itemScores.begin(), itemScores.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });

        // 返回Top-K
        results.assign(itemScores.begin(), itemScores.begin() + topK);

        return results;
    }

    /**
     * @brief 获取排序模型
     */
    RankingModel &getModel()
    {
        return rankingModel_;
    }

  private:
    FeatureStore *featureStore_; // 特征存储服务指针
    RankingModel rankingModel_;  // 排序模型实例
};
