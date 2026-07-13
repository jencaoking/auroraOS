#ifndef AURORA_SIGNAL_HPP
#define AURORA_SIGNAL_HPP

#include "task.hpp"

// ========================================================
// POSIX 信号标准 API
// ========================================================

// 1. 注册当前线程的信号处理回调函数
inline SignalHandler signal(int sig, SignalHandler handler) {
    if (sig < 1 || sig >= 16) return nullptr;
    
    TaskControlBlock* current = Scheduler::instance().get_current_tcb();
    SignalHandler old = current->signal_handlers[sig];
    current->signal_handlers[sig] = handler;
    return old;
}

// 2. 向指定 ID 的任务发送信号
inline int kill(uint32_t target_task_id, int sig) {
    if (sig < 1 || sig >= 16) return -1;

    TaskControlBlock* target = Scheduler::instance().get_task_by_id(target_task_id);
    if (!target) return -1;

    // 在位图中打上对应信号的标记
    {
        IrqGuard guard;
        target->pending_signals |= (1 << sig);
    }

    if (sig == SIGKILL) {
        // Will be handled in dispatch_signals by the scheduler, but we shouldn't directly tear the state here.
        // Actually, if we kill a sleeping or blocked task, we need it to wake up to process the signal.
        if (target->state != TaskState::Ready && target->state != TaskState::Running) {
            Scheduler::instance().set_task_state(target->id, TaskState::Ready);
        }
    } else if (target->state == TaskState::Sleeping || target->state == TaskState::Blocked_On_Notify) {
        Scheduler::instance().set_task_state(target->id, TaskState::Ready);
    }

    Scheduler::instance().schedule();
    return 0;
}

// 3. 向自己发送信号
inline int raise(int sig) {
    TaskControlBlock* current = Scheduler::instance().get_current_tcb();
    return kill(current->id, sig);
}

#endif
