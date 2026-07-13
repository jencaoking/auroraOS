#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"
#include "syscall.hpp"
#include "timer.hpp"
#include "work_queue.hpp"
#include "mpu.hpp"
#include "frame_scheduler_v2.hpp"

// 供 PendSV 汇编读取的两个全局 TCB 指针
// 声明为非 volatile：汇编直接使用符号地址，编译器临界区内通过 Arch:: 保护
extern "C" {
    TaskControlBlock* volatile g_current_tcb_ptr = nullptr;
    TaskControlBlock* volatile g_next_tcb_ptr    = nullptr;

    // 由 PendSV_Handler 调用的 MPU 动态沙盒切换
    void mpu_switch_sandbox(TaskControlBlock* next) {
        if (next && next->size_pow2 > 0) {
            MPU::instance().update_user_sandbox(next->stack_base, next->size_pow2);
        }
    }
}

// 系统 Tick 计数器（全局可见，供 lwIP OSAL 等读取系统时间）
volatile uint32_t tick_count = 0;

extern "C" {
    // ================================================================
    // SVC 分发处理函数（由 boot.S 中的 SVC_Handler 调用）
    // frame 是硬件自动压栈的寄存器快照，通过它读取系统调用参数
    // ================================================================
    void SVC_Handler_C(InterruptFrame* frame) {
        // 通过 PC 回溯到 SVC 指令，提取 8 位系统调用号
        const uint16_t svc_instr = reinterpret_cast<const uint16_t*>(frame->pc)[-1];
        const uint8_t  svc_number = static_cast<uint8_t>(svc_instr & 0xFF);

        switch (svc_number) {
            case SYS_PRINT: // SysCall: 串口输出（在内核特权态安全调用）
                uart_puts(reinterpret_cast<const char*>(frame->r0));
                break;
            case SYS_YIELD: // SysCall: 任务 Yield
                Scheduler::instance().schedule();
                break;
            case SYS_SLEEP: // SysCall: 任务 Sleep
                Scheduler::instance().sleep_ms(frame->r0);
                break;
            default:
                uart_puts("[Kernel] Unknown SVC ID!\n");
                break;
        }
    }

    // ================================================================
    // 内存管理异常处理（捕捉 MPU 违规访问）
    // ================================================================
    void MemManage_Handler(void) {
        uart_puts("\r\n[MemManage_Handler] Memory Protection Violation Detected! \r\n");
        uart_puts("Access Denied! Offending thread terminated by kernel.\r\n");
        
        // 【系统审查修复】：不要挂起整个系统，直接销毁违规线程，并将 CPU 让给其它存活任务
        TaskControlBlock* current = Scheduler::instance().get_current_tcb();
        if (current) {
            current->state = TaskState::Terminated; // 或者直接发送 SIGKILL
        }
        
        // 强制触发一次调度，让出 CPU
        Scheduler::instance().schedule();
        
        // 因为是从异常上下文调用 schedule()，在 PendSV 处理前我们必须死循环等待，
        // PendSV 发生后，由于当前线程已终止，它将永远不会再被调度回来。
        while (1) {} 
    }

    void HardFault_Handler(void) {
        uart_puts("\r\n[HardFault_Handler] Hard Fault Detected! System Halted.\r\n");
        while (1) {}
    }
}

// ================================================================
// SysTick 中断：系统心跳，驱动两件事：
//   1. tick_update()  — 将到期的休眠任务唤醒（设为 Ready）
//   2. schedule()     — 每 10ms 触发一次调度：
//                       * 高优先级任务唤醒后立即抢占低优先级
//                       * 同级任务轮转时间片
// ================================================================

#include "frame_scheduler.hpp"

extern "C" bool frame_scheduler_is_task_allowed(uint8_t priority) {
    return FrameSchedulerV2::instance().is_task_allowed(priority);
}

void SysTick_Handler(void) {
    tick_count++;
    
    // 1. 驱动软件定时器引擎
    TimerManager::instance().on_tick();

    // 2. 【核心注入】驱动蓝河帧感知时钟窗 (计算 33ms 边界)
    FrameSchedulerV2::instance().on_tick();

    Scheduler& sched = Scheduler::instance();
    sched.tick_update();
    
    // 每 5ms 触发一次高频时间片重新评估，保障 30fps 窗口内的微秒级响应
    if (tick_count % 5 == 0) {
        g_current_tcb_ptr = sched.get_current_tcb();
        sched.schedule(); 
        g_next_tcb_ptr = sched.get_current_tcb();
    }
}
