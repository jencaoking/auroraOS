#include <gtest/gtest.h>
#include "kernel/task.hpp"
#include "kernel/mutex.hpp"

class MutexTest : public ::testing::Test {
protected:
    void SetUp() override {
        Scheduler::instance().init();
        // Create an idle task so the scheduler has something to fall back to
        Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Idle);
    }
    
    void TearDown() override {
    }
};

// Test recursive lock
TEST_F(MutexTest, RecursiveLock) {
    Mutex m;
    
    // Simulate being in a task context
    TaskControlBlock* current = Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Normal);
    Arch::disable_interrupts();
    g_current_tcb_ptr = current;
    Arch::enable_interrupts();
    
    // First lock
    EXPECT_TRUE(m.lock());
    
    // Recursive lock
    EXPECT_TRUE(m.lock());
    
    // Recursive lock
    EXPECT_TRUE(m.lock());
    
    // Unlock once
    m.unlock();
    
    // Should still be locked (recursion depth 2)
    
    // Unlock twice
    m.unlock();
    
    // Unlock fully
    m.unlock();
}

// Test timeout logic
TEST_F(MutexTest, LockTimeout) {
    Mutex m;
    
    // task1 takes the lock
    TaskControlBlock* task1 = Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Normal);
    g_current_tcb_ptr = task1;
    EXPECT_TRUE(m.lock());
    
    // task2 tries to take the lock with a timeout
    TaskControlBlock* task2 = Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Normal);
    g_current_tcb_ptr = task2;
    
    bool acquired = m.lock(5); 
    EXPECT_FALSE(acquired); // task1 owns it, so task2 should timeout
    
    // Now task1 releases it
    g_current_tcb_ptr = task1;
    m.unlock();
    
    // task2 tries again and should succeed
    g_current_tcb_ptr = task2;
    EXPECT_TRUE(m.lock(5));
}

// Test UniqueLock RAII
TEST_F(MutexTest, UniqueLockRAII) {
    Mutex m;
    
    TaskControlBlock* task1 = Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Normal);
    g_current_tcb_ptr = task1;

    {
        UniqueLock lock(m, 10);
        EXPECT_TRUE(lock.owns_lock());
        
        // Recursive lock using UniqueLock
        UniqueLock recursive_lock(m, 10);
        EXPECT_TRUE(recursive_lock.owns_lock());
    } // Both destructors run, lock is fully released
    
    // Task 2 can now acquire
    TaskControlBlock* task2 = Scheduler::instance().create_task([](){}, nullptr, 0, TaskPriority::Normal);
    g_current_tcb_ptr = task2;
    UniqueLock lock2(m, 5);
    EXPECT_TRUE(lock2.owns_lock());
}
