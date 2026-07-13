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
    
    Semaphore items_;
    Semaphore spaces_;

public:
    void init() {
        items_.init(0);
        spaces_.init(Capacity);
    }

    // 生产者调用：向队列发送消息（追加到队尾，普通优先级）
    void push(const T& item) {
        spaces_.wait(); // 如果队列满了，生产者会在这里阻塞休眠

        {
            IrqGuard guard;
            buffer_[tail_] = item;
            tail_ = (tail_ + 1) % Capacity;
        }

        items_.signal(); // 通知消费者：有新消息啦！
    }

    // 生产者调用：向队列发送高优先级消息（插队到队头，紧急优先级）
    void push_urgent(const T& item) {
        spaces_.wait();

        {
            IrqGuard guard;
            // 头部逆向移动（带环绕保护）
            head_ = (head_ - 1 + Capacity) % Capacity;
            buffer_[head_] = item;
        }

        items_.signal();
    }

    // 消费者调用：从队列获取消息
    T pop() {
        items_.wait(); // 如果队列空了，消费者会在这里阻塞休眠等待
        T item;
        
        {
            IrqGuard guard;
            item = buffer_[head_];
            head_ = (head_ + 1) % Capacity;
        }

        spaces_.signal(); // 通知生产者：腾出一个空槽位啦！
        return item;
    }

    // 非阻塞生产者：队列已满时立即返回 false
    bool try_push(const T& item) {
        if (!spaces_.try_wait()) return false; // 队列已满，不阻塞

        {
            IrqGuard guard;
            buffer_[tail_] = item;
            tail_ = (tail_ + 1) % Capacity;
        }

        items_.signal();
        return true;
    }

    // 非阻塞生产者（高优插队）
    bool try_push_urgent(const T& item) {
        if (!spaces_.try_wait()) return false;

        {
            IrqGuard guard;
            head_ = (head_ - 1 + Capacity) % Capacity;
            buffer_[head_] = item;
        }

        items_.signal();
        return true;
    }

    // 非阻塞消费者
    bool try_pop(T& out) {
        if (!items_.try_wait()) return false; // 队列为空，不阻塞

        {
            IrqGuard guard;
            out = buffer_[head_];
            head_ = (head_ + 1) % Capacity;
        }

        spaces_.signal();
        return true;
    }
};

#endif
