#ifndef MUTEX_HPP
#define MUTEX_HPP

#include "task.hpp"
#include "syscall.hpp"

class Mutex {
private:
    volatile bool locked_ = false;
    TaskControlBlock* owner_ = nullptr;

public:
    void lock() {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        while (true) {
            Arch::disable_interrupts();
            if (!locked_) {
                locked_ = true;
                owner_ = current;
                Arch::enable_interrupts();
                return;
            }
            
            // 优先级继承
            if (owner_ && (int)current->current_priority > (int)owner_->current_priority) {
                owner_->current_priority = current->current_priority;
            }
            Arch::enable_interrupts();
            
            // 真正让出 CPU 1ms，防止忙等
            Scheduler::instance().sleep(1);
        }
    }

    void unlock() {
        Arch::disable_interrupts();
        if (owner_) {
            owner_->current_priority = owner_->base_priority;
            owner_ = nullptr;
        }
        locked_ = false;
        Arch::enable_interrupts();
        
        // 立即触发一次调度，让可能被阻塞的高优先级任务第一时间运行
        Scheduler::instance().schedule();
    }
};

// CP.20: Use RAII, never plain lock()/unlock()
struct LockGuard {
    Mutex& m_;
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    
    // Non-copyable
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

#endif
