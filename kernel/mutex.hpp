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
    uint32_t wait_mask_ = 0;

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
        uint32_t start_tick = TimerManager::instance().get_current_tick();

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

        while (true) {
            if (!locked_) {
                waiter_counts_[wait_prio]--;
                wait_mask_ &= ~(1 << current->id);
                locked_ = true;
                owner_ = current;
                recursive_count_ = 1;
                this->next_held_ = static_cast<Mutex*>(owner_->held_mutexes);
                owner_->held_mutexes = this;
                Arch::enable_interrupts();
                return true;
            } else if (owner_ == current) {
                waiter_counts_[wait_prio]--;
                wait_mask_ &= ~(1 << current->id);
                recursive_count_++;
                Arch::enable_interrupts();
                return true;
            }
            
            uint32_t elapsed = TimerManager::instance().get_current_tick() - start_tick;
            if (timeout_ticks != 0xFFFFFFFF && elapsed >= timeout_ticks) {
                waiter_counts_[wait_prio]--;
                wait_mask_ &= ~(1 << current->id);
                update_owner_priority();
                Arch::enable_interrupts();
                return false;
            }
            
            wait_mask_ |= (1 << current->id);
            if (timeout_ticks != 0xFFFFFFFF) {
                current->sleep_ticks = timeout_ticks - elapsed;
                Scheduler::instance().set_task_state(current->id, TaskState::Sleeping);
            } else {
                Scheduler::instance().set_task_state(current->id, TaskState::Suspended);
            }
            Arch::enable_interrupts();
            Scheduler::instance().schedule();
            Arch::disable_interrupts();
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
                
                // 唤醒最高优先级的等待者
                if (wait_mask_ != 0) {
                    uint32_t best_id = 0xFFFFFFFF;
                    uint8_t best_prio = 0;
                    for (int i = 0; i < 32; i++) {
                        if (wait_mask_ & (1 << i)) {
                            TaskControlBlock* t = Scheduler::instance().get_task_by_id(i);
                            if (t && (t->state == TaskState::Suspended || t->state == TaskState::Sleeping)) {
                                uint8_t prio = static_cast<uint8_t>(t->current_priority);
                                if (best_id == 0xFFFFFFFF || prio > best_prio) {
                                    best_prio = prio;
                                    best_id = i;
                                }
                            } else {
                                wait_mask_ &= ~(1 << i);
                            }
                        }
                    }
                    if (best_id != 0xFFFFFFFF) {
                        wait_mask_ &= ~(1 << best_id);
                        Scheduler::instance().set_task_state(best_id, TaskState::Ready);
                    }
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
