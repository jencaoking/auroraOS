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
    target->pending_signals |= (1 << sig);

    // 如果任务在睡眠，且收到了致命信号，唤醒它参与调度，使其在上下文切换时触发拦截
    if (target->state == TaskState::Sleeping && sig == SIGKILL) {
        target->state = TaskState::Ready;
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
