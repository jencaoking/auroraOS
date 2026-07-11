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
};

#endif
