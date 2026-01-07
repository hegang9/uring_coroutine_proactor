#include "MemoryPool.hpp"

MemoryPool::MemoryPool(size_t blockSize)
    : blockSize_(blockSize),
      slotSize_(0),
      firstBlock_(nullptr),
      curSlot_(nullptr),
      freeList_(nullptr),
      lastSlot_(nullptr)
{
}

MemoryPool::~MemoryPool()
{
    // 释放所有内存块
    Slot *cur = firstBlock_;
    while (cur)
    {
        Slot *next = cur->next;                // 保存下一个内存块指针
        ::free(reinterpret_cast<void *>(cur)); // 这里将slot*转为void*，保证申请释放对称性，且明确MemoryPool只管理内存分配与释放，不涉及对象构造和析构，析构对象应该由调用者使用deleteElement来完成
        cur = next;
    }
}

void MemoryPool::init(size_t slotSize)
{
    slotSize_ = slotSize;
}

void *MemoryPool::allocate()
{
    // 优先使用空闲链表中的内存槽
    if (freeList_)
    {
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        if (freeList_)
        {
            Slot *slot = freeList_;
            freeList_ = freeList_->next;
            return reinterpret_cast<void *>(slot);
        }
    }
    // 如果空闲链表为空，使用当前内存块中尚未使用的内存槽
    Slot *temp = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ > lastSlot_)
        {
            // 说明此时该内存块中也没有可用的内存槽，需要申请新的内存块
            // 申请新内存块后，curSlot_与lastSlot_会更新为指向新内存块
            allocateNewBlock();
        }
        temp = curSlot_;
        curSlot_ += slotSize_ / sizeof(Slot);
    }
    return temp;
}

void MemoryPool::deallocate(void *p)
{
    // 头插法
    if (p)
    {
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        Slot *slot = reinterpret_cast<Slot *>(p);
        slot->next = freeList_;
        freeList_ = slot;
    }
}

void MemoryPool::allocateNewBlock()
{
    void *newBlock = ::malloc(blockSize_);
    if (newBlock)
    {
        // 使用头插法将新内存块插入内存块链表
        reinterpret_cast<Slot *>(newBlock)->next = firstBlock_;
        firstBlock_ = reinterpret_cast<Slot *>(newBlock);
        curSlot_ = firstBlock_;
    }
    char *endofhead = reinterpret_cast<char *>(newBlock) + sizeof(Slot *);
    size_t paddingsize = padPointer(endofhead, slotSize_);
    curSlot_ = reinterpret_cast<Slot *>(endofhead + paddingsize);

    lastSlot_ = reinterpret_cast<Slot *>(reinterpret_cast<size_t>(newBlock) + blockSize_ - slotSize_ + 1);
    freeList_ = nullptr; // 新内存块没有空闲槽，清空 freeList_
}

size_t MemoryPool::padPointer(char *p, size_t slotSize)
{
    return (slotSize - reinterpret_cast<size_t>(p)) % slotSize;
}

void HashBucket::initMemoryPool()
{
    for (int i = 1; i < MEMORY_POOL_NUM; ++i)
    {
        getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
    }
}

MemoryPool &HashBucket::getMemoryPool(int index)
{
    return memoryPool[index];
}

void *HashBucket::useMemory(size_t size)
{
    if (size <= 0)
        return nullptr;

    // 若大于512字节，直接使用系统分配
    if (size > MAX_SLOT_SIZE)
    {
        return ::malloc(size);
    }

    // 否则从内存池分配
    // 计算该size应该使用的内存池索引
    int index = (size + 7) / SLOT_BASE_SIZE - 1;
    return getMemoryPool(index).allocate();
}

void HashBucket::freeMemory(void *p, size_t size)
{
    if (!p)
        return;

    // 大于512字节，释放给系统
    if (size > MAX_SLOT_SIZE)
    {
        ::free(p);
        return;
    }

    int index = (size + 7) / SLOT_BASE_SIZE - 1;
    getMemoryPool(index).deallocate(p);
    return;
}
