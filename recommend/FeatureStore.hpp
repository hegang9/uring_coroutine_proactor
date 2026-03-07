#pragma once

/**
 * @file FeatureStore.hpp
 * @brief 推荐系统的特征存储与LRU缓存
 *
 * 设计说明：
 * 在搜广推系统中，特征是模型的关键输入。常见的特征类型包括：
 *   1. 用户特征：性别、年龄、VIP等级、历史行为序列等
 *   2. 物品特征：品类、价格、热度、评分等
 *   3. 上下文特征：时间、地点、设备等
 *   4. 交互特征：用户与物品的相似度、交叉特征等
 *
 * 在线服务对特征延迟敏感，通常使用LRU缓存加速热点访问。
 * 此处实现一个简化的内存LRU缓存，模拟：
 *   - 用户画像缓存（用户ID -> 特征向量）
 *   - 物品特征缓存（物品ID -> 特征向量）
 *
 * 真实系统的特征服务可能使用：
 *   - Redis/Memcached：分布式缓存
 *   - HBase/DynamoDB：海量特征库
 *   - 特征向量数据库：快速相似度检索
 */

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ===================== 特征向量结构 =====================

/**
 * @brief 特征向量
 *
 * 在推荐系统的排序阶段，模型通常处理稠密向量（float32 or float64）。
 * 这里使用浮点数组表示特征向量。
 *
 * 注意：
 * - 向量维度应该固定且一致（如128维、256维等）
 * - 向量通常在模型训练时确定维度和初始化方法
 */
struct FeatureVector
{
    static constexpr int kFeatureDim = 32; // 特征向量维度

    // 特征向量数据：32维浮点数
    float values[kFeatureDim] = {0.0f};

    /**
     * @brief 计算与另一个向量的余弦相似度
     *
     * 公式: cos(A, B) = (A·B) / (||A|| * ||B||)
     * 范围: [-1, 1]，值越大表示越相似
     *
     * 在推荐排序中，用于计算用户与物品的相似度得分。
     */
    float cosineSimilarity(const FeatureVector &other) const
    {
        float dotProduct = 0.0f;
        float normA = 0.0f;
        float normB = 0.0f;

        for (int i = 0; i < kFeatureDim; i++)
        {
            dotProduct += values[i] * other.values[i];
            normA += values[i] * values[i];
            normB += other.values[i] * other.values[i];
        }

        if (normA < 1e-6f || normB < 1e-6f)
            return 0.0f; // 零向量处理

        return dotProduct / (std::sqrt(normA) * std::sqrt(normB));
    }

    /**
     * @brief 生成随机特征向量（用于模拟）
     */
    void generateRandom(uint64_t seed)
    {
        // 简单的伪随机生成：基于seed的线性同余生成器
        uint64_t state = seed;
        for (int i = 0; i < kFeatureDim; i++)
        {
            state = state * 1103515245ULL + 12345ULL;
            // 归一化到 [-1, 1]
            values[i] = (((state / 65536) % 32768) / 16384.0f) - 1.0f;
        }
    }
};

// ===================== LRU缓存节点 =====================

/**
 * @brief LRU缓存中的单个节点
 *
 * 双向链表节点，用于维护访问顺序（最近使用的在前）
 */
template <typename KeyType, typename ValueType> struct LRUNode
{
    KeyType key;
    ValueType value;
    std::shared_ptr<LRUNode> prev;
    std::shared_ptr<LRUNode> next;

    LRUNode(const KeyType &k, const ValueType &v) : key(k), value(v)
    {
    }
};

// ===================== LRU缓存 =====================

/**
 * @brief 线程不安全的LRU缓存实现
 *
 * 特点：
 *   - O(1) get/put 时间复杂度（使用哈希表 + 双向链表）
 *   - 自动淘汰最久未使用的元素
 *   - 适用于单线程场景（多线程需加锁）
 *
 * 算法说明：
 *   - get(key): 返回value并将该节点移到链表头（最近使用）
 *   - put(key, value): 插入新节点或更新现有节点，维护容量限制
 *
 * 真实系统会使用：
 *   - std::unordered_map + 双向链表（本实现）
 *   - Boost.MultiIndex（多键索引）
 *   - 自定义哈希表（性能优化）
 */
template <typename KeyType, typename ValueType> class LRUCache
{
  public:
    using NodeType = LRUNode<KeyType, ValueType>;

    /**
     * @brief 初始化LRU缓存
     * @param capacity 缓存最大容量
     */
    explicit LRUCache(size_t capacity) : capacity_(capacity)
    {
        // 创建哨兵节点（虚拟头尾），简化链表操作
        head_ = std::make_shared<NodeType>(KeyType(), ValueType());
        tail_ = std::make_shared<NodeType>(KeyType(), ValueType());
        head_->next = tail_;
        tail_->prev = head_;
    }

    /**
     * @brief 从缓存获取值
     * @param key 键值
     * @param value 输出参数，若找到则填充
     * @return 是否找到
     */
    bool get(const KeyType &key, ValueType &value)
    {
        auto it = cache_.find(key);
        if (it == cache_.end())
            return false;

        auto node = it->second;
        value = node->value;

        // 移到链表头（标记为最近使用）
        moveToHead(node);
        return true;
    }

    /**
     * @brief 写入缓存
     * @param key 键值
     * @param value 值
     */
    void put(const KeyType &key, const ValueType &value)
    {
        auto it = cache_.find(key);

        if (it != cache_.end())
        {
            // 更新现有节点
            auto node = it->second;
            node->value = value;
            moveToHead(node);
            return;
        }

        // 插入新节点
        auto newNode = std::make_shared<NodeType>(key, value);
        cache_[key] = newNode;
        insertToHead(newNode);

        // 检查容量，删除最久未使用的节点
        if (cache_.size() > capacity_)
        {
            auto tailNode = tail_->prev;
            removeTail();
            cache_.erase(tailNode->key);
        }
    }

    /**
     * @brief 获取缓存的当前大小
     */
    size_t size() const
    {
        return cache_.size();
    }

    /**
     * @brief 清空缓存
     */
    void clear()
    {
        cache_.clear();
        head_->next = tail_;
        tail_->prev = head_;
    }

  private:
    /**
     * @brief 将节点移到链表头（最近使用）
     */
    void moveToHead(std::shared_ptr<NodeType> node)
    {
        removeNode(node);
        insertToHead(node);
    }

    /**
     * @brief 在链表头后插入节点
     */
    void insertToHead(std::shared_ptr<NodeType> node)
    {
        node->prev = head_;
        node->next = head_->next;
        head_->next->prev = node;
        head_->next = node;
    }

    /**
     * @brief 删除链表中的节点
     */
    void removeNode(std::shared_ptr<NodeType> node)
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    /**
     * @brief 删除链表尾部的节点（最久未使用）
     */
    void removeTail()
    {
        auto node = tail_->prev;
        removeNode(node);
    }

    size_t capacity_;
    std::shared_ptr<NodeType> head_;                               // 链表头（虚拟）
    std::shared_ptr<NodeType> tail_;                               // 链表尾（虚拟）
    std::unordered_map<KeyType, std::shared_ptr<NodeType>> cache_; // 哈希表索引
};

// ===================== 特征存储服务 =====================

/**
 * @brief 特征存储服务
 *
 * 整合用户特征、物品特征的存储与缓存。
 * 在真实系统中，这个服务可能会：
 *   1. 查询本地缓存（L1）
 *   2. 查询分布式缓存如Redis（L2）
 *   3. 查询特征数据库如HBase（L3）
 *   4. 实时计算复杂特征（如交叉特征）
 *
 * 面试经验：
 * - 关键词：多级缓存、缓存穿透、缓存雪崩、布隆过滤器
 * - 实际场景：特征延迟通常需控制在5ms以内
 */
class FeatureStore
{
  public:
    // 用户特征缓存：userId -> 用户特征向量
    using UserFeatureCache = LRUCache<uint64_t, FeatureVector>;
    // 物品特征缓存：itemId -> 物品特征向量
    using ItemFeatureCache = LRUCache<uint64_t, FeatureVector>;

    /**
     * @brief 初始化特征存储
     * @param userCacheSize 用户特征缓存大小
     * @param itemCacheSize 物品特征缓存大小
     */
    FeatureStore(size_t userCacheSize = 10000, size_t itemCacheSize = 100000)
        : userFeatureCache_(userCacheSize), itemFeatureCache_(itemCacheSize)
    {
    }

    /**
     * @brief 获取用户特征向量
     *
     * 流程：
     *   1. 先查缓存（LRU）
     *   2. 未命中则生成随机特征（模拟查库）
     *   3. 写入缓存返回
     *
     * @param userId 用户ID
     * @return 用户特征向量
     */
    FeatureVector getUserFeature(uint64_t userId)
    {
        FeatureVector feature;

        // 尝试从缓存获取
        if (userFeatureCache_.get(userId, feature))
        {
            cacheHits_++;
            return feature;
        }

        cacheMisses_++;

        // 缓存未命中，模拟从数据库查询（实际应查HBase/特征服务）
        // 这里使用userId作为seed生成伪随机特征向量
        feature.generateRandom(userId);

        // 写入缓存
        userFeatureCache_.put(userId, feature);
        return feature;
    }

    /**
     * @brief 获取物品特征向量
     *
     * 同上，但用于物品特征。
     * 物品特征一般比用户特征更新频率低，可以有更大的缓存。
     */
    FeatureVector getItemFeature(uint64_t itemId)
    {
        FeatureVector feature;

        if (itemFeatureCache_.get(itemId, feature))
        {
            itemCacheHits_++;
            return feature;
        }

        itemCacheMisses_++;

        // 模拟特征查询
        feature.generateRandom(itemId);
        itemFeatureCache_.put(itemId, feature);
        return feature;
    }

    /**
     * @brief 获取缓存统计信息
     */
    struct CacheStats
    {
        uint64_t userCacheHits = 0;
        uint64_t userCacheMisses = 0;
        uint64_t itemCacheHits = 0;
        uint64_t itemCacheMisses = 0;
        float userHitRate = 0.0f;
        float itemHitRate = 0.0f;
    };

    CacheStats getCacheStats() const
    {
        CacheStats stats;
        stats.userCacheHits = cacheHits_;
        stats.userCacheMisses = cacheMisses_;
        stats.itemCacheHits = itemCacheHits_;
        stats.itemCacheMisses = itemCacheMisses_;

        uint64_t totalUser = cacheHits_ + cacheMisses_;
        uint64_t totalItem = itemCacheHits_ + itemCacheMisses_;

        stats.userHitRate = (totalUser > 0) ? (100.0f * cacheHits_ / totalUser) : 0.0f;
        stats.itemHitRate = (totalItem > 0) ? (100.0f * itemCacheHits_ / totalItem) : 0.0f;

        return stats;
    }

  private:
    UserFeatureCache userFeatureCache_; // 用户特征缓存
    ItemFeatureCache itemFeatureCache_; // 物品特征缓存

    // 统计信息
    uint64_t cacheHits_ = 0;
    uint64_t cacheMisses_ = 0;
    uint64_t itemCacheHits_ = 0;
    uint64_t itemCacheMisses_ = 0;
};
