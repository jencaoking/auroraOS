#ifndef MSG_QUEUE_HPP
#define MSG_QUEUE_HPP

#include "mutex.hpp"
#include "semaphore.hpp"

// 裸机环境下的定长泛型消息队列
template <typename T, int Capacity>
class MessageQueue {
private:
    T buffer_[Capacity];
    int head_ = 0;
    int tail_ = 0;
    
    Mutex mutex_;
    Semaphore items_;
    Semaphore spaces_;

    inline void disable_interrupts() { __asm__ volatile ("cpsid i" : : : "memory"); }
    inline void enable_interrupts()  { __asm__ volatile ("cpsie i" : : : "memory"); }

public:
    void init() {
        items_.init(0);
        spaces_.init(Capacity);
    }

    // 生产者调用：向队列发送消息
    void push(const T& item) {
        spaces_.wait(); // 如果队列满了，生产者会在这里阻塞休眠

        mutex_.lock();
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % Capacity;
        mutex_.unlock();

        items_.signal(); // 通知消费者：有新消息啦！
    }

    // 消费者调用：从队列获取消息
    T pop() {
        items_.wait(); // 如果队列空了，消费者会在这里阻塞休眠等待

        mutex_.lock();
        T item = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        mutex_.unlock();

        spaces_.signal(); // 通知生产者：腾出一个空槽位啦！
        return item;
    }

    // 非阻塞生产者：队列已满时立即返回 false，绝不让出 CPU 或调用系统调用。
    // 专为中断上下文 (ISR) / trypost 语义设计——临界区用关中断保护，
    // 而不是会在争用时经由 sys_yield() 触发 SVC 的 Mutex，因此可以安全地
    // 从 ISR 里调用（不会像原先转调阻塞版 push() 那样在队列满时死等）。
    bool try_push(const T& item) {
        if (!spaces_.try_wait()) return false; // 队列已满，不阻塞

        disable_interrupts();
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % Capacity;
        enable_interrupts();

        items_.signal();
        return true;
    }

    // 非阻塞消费者：队列为空时立即返回 false。用于 tryfetch 语义
    // （lwIP 主循环需要用它做"看看有没有消息，没有就继续处理定时器"的轮询，
    // 绝不能阻塞，否则会卡死协议栈主线程）。
    bool try_pop(T& out) {
        if (!items_.try_wait()) return false; // 队列为空，不阻塞

        disable_interrupts();
        out = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        enable_interrupts();

        spaces_.signal();
        return true;
    }
};

#endif
