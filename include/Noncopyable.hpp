#pragma once

/**
 * 继承该类的派生类对象可以正常构造和析构，但不允许拷贝和赋值。
 * 需要私有继承，以防止通过基类指针或引用删除派生类对象。
 */

class Noncopyable
{
protected:
    Noncopyable() = default;
    ~Noncopyable() = default;

public:
    Noncopyable(const Noncopyable &) = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;
};