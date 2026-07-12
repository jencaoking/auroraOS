#ifndef MUTEX_HPP
#define MUTEX_HPP

#include "task.hpp"
#include "syscall.hpp"

class Mutex {
private:
    volatile bool locked_ = false;
    TaskControlBlock* owner_ = nullptr;
    uint32_t recursive_count_ = 0;
    Mutex* next_held_ = nullptr;
    uint8_t waiter_counts_[5] = {0};

    uint8_t get_highest_waiter() {
        for (int i = 4; i >= 0; i--) {
            if (waiter_counts_[i] > 0) return i;
        }
        return 0; // No waiters
    }

    void update_owner_priority() {
        if (!owner_) return;
        uint8_t max_prio = static_cast<uint8_t>(owner_->base_priority);
        Mutex* m = static_cast<Mutex*>(owner_->held_mutexes);
        while (m) {
            uint8_t highest_waiter = m->get_highest_waiter();
            if (highest_waiter > max_prio) {
                max_prio = highest_waiter;
            }
            m = m->next_held_;
        }
        if (max_prio != static_cast<uint8_t>(owner_->current_priority)) {
            Scheduler::instance().set_task_priority(owner_->id, static_cast<TaskPriority>(max_prio));
        }
    }

public:
    bool lock(uint32_t timeout_ticks = 0xFFFFFFFF) {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        uint32_t elapsed = 0;

        Arch::disable_interrupts();
        if (!locked_) {
            locked_ = true;
            owner_ = current;
            recursive_count_ = 1;
            this->next_held_ = static_cast<Mutex*>(owner_->held_mutexes);
            owner_->held_mutexes = this;
            Arch::enable_interrupts();
            return true;
        } else if (owner_ == current) {
            recursive_count_++;
            Arch::enable_interrupts();
            return true;
        }

        uint8_t wait_prio = static_cast<uint8_t>(current->current_priority);
        waiter_counts_[wait_prio]++;
        
        // 优先级继承
        if (owner_ && wait_prio > static_cast<uint8_t>(owner_->current_priority)) {
            Scheduler::instance().set_task_priority(owner_->id, static_cast<TaskPriority>(wait_prio));
        }
        Arch::enable_interrupts();

        while (true) {
            Arch::disable_interrupts();
            if (!locked_) {
                waiter_counts_[wait_prio]--;
                locked_ = true;
                owner_ = current;
                recursive_count_ = 1;
                this->next_held_ = static_cast<Mutex*>(owner_->held_mutexes);
                owner_->held_mutexes = this;
                Arch::enable_interrupts();
                return true;
            } else if (owner_ == current) {
                waiter_counts_[wait_prio]--;
                recursive_count_++;
                Arch::enable_interrupts();
                return true;
            }
            
            if (timeout_ticks != 0xFFFFFFFF && elapsed >= timeout_ticks) {
                waiter_counts_[wait_prio]--;
                update_owner_priority();
                Arch::enable_interrupts();
                return false;
            }
            Arch::enable_interrupts();
            
            if (timeout_ticks != 0xFFFFFFFF) elapsed++;
            Scheduler::instance().sleep(1);
        }
    }

    void unlock() {
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        Arch::disable_interrupts();
        
        if (owner_ == current) {
            recursive_count_--;
            if (recursive_count_ == 0) {
                // 从持有的锁链表中移除自身
                Mutex** curr_ptr = reinterpret_cast<Mutex**>(&owner_->held_mutexes);
                while (*curr_ptr) {
                    if (*curr_ptr == this) {
                        *curr_ptr = this->next_held_;
                        break;
                    }
                    curr_ptr = &(*curr_ptr)->next_held_;
                }
                this->next_held_ = nullptr;
                
                TaskControlBlock* old_owner = owner_;
                owner_ = nullptr;
                locked_ = false;

                // 重新计算原拥有者的优先级 (可能还持有其它被高优先级等待的锁)
                uint8_t max_prio = static_cast<uint8_t>(old_owner->base_priority);
                Mutex* m = static_cast<Mutex*>(old_owner->held_mutexes);
                while (m) {
                    uint8_t highest_waiter = m->get_highest_waiter();
                    if (highest_waiter > max_prio) {
                        max_prio = highest_waiter;
                    }
                    m = m->next_held_;
                }
                if (max_prio != static_cast<uint8_t>(old_owner->current_priority)) {
                    Scheduler::instance().set_task_priority(old_owner->id, static_cast<TaskPriority>(max_prio));
                }
                
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
