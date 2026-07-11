#ifndef MUTEX_HPP
#define MUTEX_HPP

#include "task.hpp"

class Mutex {
private:
    bool locked_ = false;

    // 内联汇编：关闭/开启全局中断
    inline void disable_interrupts() { __asm__ volatile ("cpsid i" : : : "memory"); }
    inline void enable_interrupts()  { __asm__ volatile ("cpsie i" : : : "memory"); }

public:
    void lock() {
        while (true) {
            disable_interrupts();
            if (!locked_) {
                locked_ = true;
                enable_interrupts();
                return; // 成功获取锁
            }
            enable_interrupts();
            
            // 锁被占用，当前线程主动让出 CPU 给其他线程（Yield）
            Scheduler::instance().schedule();
        }
    }

    void unlock() {
        disable_interrupts();
        locked_ = false;
        enable_interrupts();
    }
};

#endif
