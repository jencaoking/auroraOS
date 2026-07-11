#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"
#include "syscall.hpp"
#include "timer.hpp"
#include "work_queue.hpp"

// 供 PendSV 汇编读取的两个全局 TCB 指针
// 声明为非 volatile：汇编直接使用符号地址，编译器临界区内通过 Arch:: 保护
extern "C" {
    TaskControlBlock* volatile g_current_tcb_ptr = nullptr;
    TaskControlBlock* volatile g_next_tcb_ptr    = nullptr;
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
                Scheduler::instance().sleep(frame->r0);
                break;
            default:
                uart_puts("[Kernel] Unknown SVC ID!\n");
                break;
        }
    }
}

// ================================================================
// SysTick 中断：系统心跳，驱动两件事：
//   1. tick_update()  — 将到期的休眠任务唤醒（设为 Ready）
//   2. schedule()     — 每 10ms 触发一次调度：
//                       * 高优先级任务唤醒后立即抢占低优先级
//                       * 同级任务轮转时间片
// ================================================================

void SysTick_Handler(void) {
    tick_count++;
    
    // 1. 驱动软件定时器引擎
    TimerManager::instance().on_tick();

    // 2. 工作队列测试：每 3 秒从中断环境提交一个耗时任务
    if (tick_count % 3000 == 0) {
        WorkQueue::instance().submit_from_isr([](void* arg) {
            sys_print("\r\n[WorkQueue Daemon] Background job started. Simulating heavy work...\r\n");
            Scheduler::instance().sleep(500); 
            sys_print("[WorkQueue Daemon] Heavy job completed!\r\n");
        }, nullptr);
    }

    Scheduler& sched = Scheduler::instance();

    // Step 1: 更新所有休眠任务的倒计时
    sched.tick_update();

    // Step 2: 周期性调度（含优先级抢占检测）
    // schedule() 内部会原子地更新 g_current_tcb_ptr / g_next_tcb_ptr 并 pending PendSV
    if (tick_count % 10 == 0) {
        sched.schedule();
    }
}
