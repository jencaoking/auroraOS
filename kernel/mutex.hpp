#ifndef MUTEX_HPP
#define MUTEX_HPP

#include "task.hpp"
#include "syscall.hpp"

class Mutex {
private:
    volatile bool locked_ = false;
    TaskControlBlock* owner_ = nullptr;
    uint32_t recursive_count_ = 0;

public:
    bool lock(uint32_t timeout_ticks = 0xFFFFFFFF) {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        uint32_t elapsed = 0;

        while (true) {
            Arch::disable_interrupts();
            if (!locked_) {
                locked_ = true;
                owner_ = current;
                recursive_count_ = 1;
                Arch::enable_interrupts();
                return true;
            } else if (owner_ == current) {
                recursive_count_++;
                Arch::enable_interrupts();
                return true;
            }
            
            // 优先级继承
            if (owner_ && (int)current->current_priority > (int)owner_->current_priority) {
                Scheduler::instance().set_task_priority(owner_->id, current->current_priority);
            }
            Arch::enable_interrupts();
            
            if (timeout_ticks != 0xFFFFFFFF) {
                if (elapsed >= timeout_ticks) {
                    return false;
                }
                elapsed++;
            }
            Scheduler::instance().sleep(1);
        }
    }

    void unlock() {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        Arch::disable_interrupts();
        
        if (owner_ == current) {
            recursive_count_--;
            if (recursive_count_ == 0) {
                Scheduler::instance().set_task_priority(owner_->id, owner_->base_priority);
                owner_ = nullptr;
                locked_ = false;
                Arch::enable_interrupts();
                
                // 立即触发一次调度，让可能被阻塞的高优先级任务第一时间运行
                Scheduler::instance().schedule();
                return;
            }
        }
        Arch::enable_interrupts();
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

struct UniqueLock {
    Mutex& m_;
    bool owns_lock_;

    explicit UniqueLock(Mutex& m, uint32_t timeout_ticks = 0xFFFFFFFF) : m_(m) { 
        owns_lock_ = m_.lock(timeout_ticks); 
    }
    ~UniqueLock() { 
        if (owns_lock_) {
            m_.unlock(); 
        }
    }
    
    bool owns_lock() const { return owns_lock_; }

    // Non-copyable
    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;
};

#endif
