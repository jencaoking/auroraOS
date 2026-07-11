#ifndef SEMAPHORE_HPP
#define SEMAPHORE_HPP

#include "task.hpp"

class Semaphore {
private:
    int count_;

    inline void disable_interrupts() { __asm__ volatile ("cpsid i" : : : "memory"); }
    inline void enable_interrupts()  { __asm__ volatile ("cpsie i" : : : "memory"); }

public:
    // 初始化时指定资源的初始数量
    constexpr Semaphore(int init_count = 0) : count_(init_count) {}
    
    void init(int init_count) {
        count_ = init_count;
    }

    // 消费者等待资源
    void wait() {
        while (true) {
            disable_interrupts();
            if (count_ > 0) {
                count_--;
                enable_interrupts();
                return; // 成功获取资源
            }
            enable_interrupts();
            
            // 没有资源，主动让出 CPU (Yield) 并在后台轮询
            Scheduler::instance().schedule();
        }
    }

    // 生产者释放/增加资源
    void signal() {
        disable_interrupts();
        count_++;
        enable_interrupts();
    }
};

#endif
