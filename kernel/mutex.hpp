#ifndef MUTEX_HPP
#define MUTEX_HPP

#include "task.hpp"
#include "syscall.hpp"
#include "timer.hpp"

class Mutex {
private:
    volatile bool locked_ = false;
    TaskControlBlock* owner_ = nullptr;
    uint32_t recursive_count_ = 0;
    Mutex* next_held_ = nullptr;
    uint32_t wait_mask_ = 0;

    uint8_t get_highest_waiter() {
        uint8_t max_prio = 0;
        for (int i = 0; i < 32; i++) {
            if (wait_mask_ & (1 << i)) {
                TaskControlBlock* t = Scheduler::instance().get_task_by_id(i);
                if (t && static_cast<uint8_t>(t->current_priority) > max_prio) {
                    max_prio = static_cast<uint8_t>(t->current_priority);
                }
            }
        }
        return max_prio;
    }

    static void propagate_priority(TaskControlBlock* start_task) {
        TaskControlBlock* task = start_task;
        while (task->waiting_on_mutex) {
            Mutex* m = task->waiting_on_mutex;
            TaskControlBlock* owner = m->owner_;
            if (!owner) break;
            
            if (static_cast<uint8_t>(task->current_priority) > static_cast<uint8_t>(owner->current_priority)) {
                Scheduler::instance().set_task_priority(owner->id, task->current_priority);
                task = owner;
            } else {
                break;
            }
        }
    }

    static void recalculate_priority_chain(TaskControlBlock* start_task) {
        TaskControlBlock* task = start_task;
        while (task) {
            uint8_t max_prio = static_cast<uint8_t>(task->base_priority);
            Mutex* m = static_cast<Mutex*>(task->held_mutexes);
            while (m) {
                uint8_t highest_waiter = m->get_highest_waiter();
                if (highest_waiter > max_prio) {
                    max_prio = highest_waiter;
                }
                m = m->next_held_;
            }
            
            if (max_prio != static_cast<uint8_t>(task->current_priority)) {
                Scheduler::instance().set_task_priority(task->id, static_cast<TaskPriority>(max_prio));
                if (task->waiting_on_mutex && task->waiting_on_mutex->owner_) {
                    task = task->waiting_on_mutex->owner_;
                } else {
                    break;
                }
            } else {
                break;
            }
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
            if (owner_) {
                this->next_held_ = static_cast<Mutex*>(owner_->held_mutexes);
                owner_->held_mutexes = this;
            }
            Arch::enable_interrupts();
            return true;
        } else if (owner_ && owner_ == current) {
            recursive_count_++;
            Arch::enable_interrupts();
            return true;
        }

        if (!current) {
            // 调度器未启动，但发生资源竞争，只能死锁挂起或直接返回
            Arch::enable_interrupts();
            return false;
        }

        uint8_t wait_prio = static_cast<uint8_t>(current->current_priority);
        
        current->waiting_on_mutex = this;
        // 优先级继承传播
        if (owner_ && wait_prio > static_cast<uint8_t>(owner_->current_priority)) {
            propagate_priority(current);
        }

        while (true) {
            if (!locked_) {
                wait_mask_ &= ~(1 << current->id);
                current->waiting_on_mutex = nullptr;
                locked_ = true;
                owner_ = current;
                recursive_count_ = 1;
                this->next_held_ = static_cast<Mutex*>(owner_->held_mutexes);
                owner_->held_mutexes = this;
                Arch::enable_interrupts();
                return true;
            } else if (owner_ && owner_ == current) {
                wait_mask_ &= ~(1 << current->id);
                current->waiting_on_mutex = nullptr;
                recursive_count_++;
                Arch::enable_interrupts();
                return true;
            }
            
            uint32_t elapsed = TimerManager::instance().get_current_tick() - start_tick;
            if (timeout_ticks != 0xFFFFFFFF && elapsed >= timeout_ticks) {
                wait_mask_ &= ~(1 << current->id);
                current->waiting_on_mutex = nullptr;
                if (owner_) recalculate_priority_chain(owner_);
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
        
        if (locked_ && owner_ == current) {
            recursive_count_--;
            if (recursive_count_ == 0) {
                // 从持有的锁链表中移除自身
                if (owner_) {
                    Mutex** curr_ptr = reinterpret_cast<Mutex**>(&owner_->held_mutexes);
                    while (*curr_ptr) {
                        if (*curr_ptr == this) {
                            *curr_ptr = this->next_held_;
                            break;
                        }
                        curr_ptr = &(*curr_ptr)->next_held_;
                    }
                }
                this->next_held_ = nullptr;
                
                TaskControlBlock* old_owner = owner_;
                owner_ = nullptr;
                locked_ = false;

                // 重新计算原拥有者的优先级并可能传播
                if (old_owner) {
                    recalculate_priority_chain(old_owner);
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

    void force_unlock(TaskControlBlock* target_owner) {
        Arch::disable_interrupts();
        if (locked_ && owner_ == target_owner) {
            // 从持有的锁链表中移除自身
            if (owner_) {
                Mutex** curr_ptr = reinterpret_cast<Mutex**>(&owner_->held_mutexes);
                while (*curr_ptr) {
                    if (*curr_ptr == this) {
                        *curr_ptr = this->next_held_;
                        break;
                    }
                    curr_ptr = &(*curr_ptr)->next_held_;
                }
            }
            this->next_held_ = nullptr;

            owner_ = nullptr;
            locked_ = false;
            recursive_count_ = 0;
            
            if (target_owner) {
                recalculate_priority_chain(target_owner);
            }
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
