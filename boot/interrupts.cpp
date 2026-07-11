#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"

// 供汇编读取的两个全局 TCB 指针
extern "C" {
    volatile TaskControlBlock* g_current_tcb_ptr = nullptr;
    volatile TaskControlBlock* g_next_tcb_ptr = nullptr;
}

volatile uint32_t tick_count = 0;



extern "C" {
    // SVC 中断的处理逻辑
    // frame 是由硬件自动压入栈中的上下文指针
    void SVC_Handler_C(InterruptFrame* frame) {
        // SVC 指令会带一个 8 位的立即数（SVC ID），
        // 我们可以通过 PC 指针回溯到 SVC 指令所在位置，读取其中的立即数
        uint32_t* svc_pc = (uint32_t*)frame->pc;
        uint16_t svc_instr = ((uint16_t*)svc_pc)[-1];
        uint8_t svc_number = svc_instr & 0xFF;

        // 根据不同的 ID 处理不同的系统调用
        switch (svc_number) {
            case 0x01: // SysCall: 串口输出
                // 这里我们可以在内核特权模式下调用原本的串口打印
                uart_puts((const char*)frame->r0); 
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

void SysTick_Handler(void) {
    tick_count++;
    Scheduler& sched = Scheduler::instance();
    
    // 更新所有处于 Sleeping 状态的线程
    sched.tick_update();
    
    // 每 10ms 强制进行一次时间片轮转（如果线程没被阻塞的话）
    if (tick_count % 10 == 0) {
        sched.schedule(); 
    }
}
