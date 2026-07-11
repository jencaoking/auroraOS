#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"

// 供 PendSV 汇编读取的两个全局 TCB 指针
// 声明为非 volatile：汇编直接使用符号地址，编译器临界区内通过 Arch:: 保护
extern "C" {
    TaskControlBlock* g_current_tcb_ptr = nullptr;
    TaskControlBlock* g_next_tcb_ptr    = nullptr;
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
            case 0x01: // SysCall: 串口输出（在内核特权态安全调用）
                uart_puts(reinterpret_cast<const char*>(frame->r0));
                break;
            case 0x02: // SysCall: 任务 Yield
                Scheduler::instance().schedule();
                break;
            case 0x03: // SysCall: 任务 Sleep
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
    Scheduler& sched = Scheduler::instance();

    // Step 1: 更新所有休眠任务的倒计时
    sched.tick_update();

    // Step 2: 周期性调度（含优先级抢占检测）
    // schedule() 内部会原子地更新 g_current_tcb_ptr / g_next_tcb_ptr 并 pending PendSV
    if (tick_count % 10 == 0) {
        sched.schedule();
    }
}
