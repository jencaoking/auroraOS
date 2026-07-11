#ifndef AURORA_MUTEX_PI_HPP
#define AURORA_MUTEX_PI_HPP

#include "task.hpp"

class MutexPI {
private:
    bool locked_ = false;
    TaskControlBlock* owner_ = nullptr; // 记录当前持有锁的任务

public:
    void lock() {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        
        while (true) {
            Arch::disable_interrupts();
            
            if (!locked_) {
                // 成功抢到锁，登记所有者
                locked_ = true;
                owner_ = current;
                Arch::enable_interrupts();
                return; 
            }
            
            // 锁被别人拿着。触发【优先级继承】核心逻辑：
            // 如果我 (current) 的优先级比拿着锁的那个家伙 (owner) 高，
            // 强行把他的优先级拔高到跟我一样！
            if (owner_ && (int)current->current_priority > (int)owner_->current_priority) {
                owner_->current_priority = current->current_priority;
            }
            
            Arch::enable_interrupts();
            
            // 拿不到锁，让出 CPU
            Scheduler::instance().schedule();
        }
    }

    void unlock() {
        Arch::disable_interrupts();
        
        if (owner_) {
            // 【关键恢复】释放锁时，必须把自己的优先级打回原形
            owner_->current_priority = owner_->base_priority;
            owner_ = nullptr;
        }
        locked_ = false;
        
        Arch::enable_interrupts();
        
        // 释放锁后立即触发一次调度，让刚才被阻塞的高优先级任务能第一时间抢占回来
        Scheduler::instance().schedule();
    }
};

#endif
