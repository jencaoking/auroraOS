#include "uart.h"
#include "config.h"
#include "interrupts.hpp"
#include "task.hpp"

// 为两个新任务开辟独立的栈空间 (防止它们互相踩内存)
uint32_t task1_stack[256];
uint32_t task2_stack[256];

// 任务 1：不断输出 A
extern "C" void task1(void) {
    while (1) {
        uart_putc('A');
        // 极简死循环延时，防止串口刷得太快
        for (volatile int i = 0; i < 50000; i++); 
    }
}

// 任务 2：不断输出 B
extern "C" void task2(void) {
    while (1) {
        uart_putc('B');
        for (volatile int i = 0; i < 50000; i++);
    }
}

extern "C" void kernel_main(void) {
    uart_init();
    
    // 1. 初始化内核调度器
    Scheduler& sched = Scheduler::instance();
    sched.init();

    // 2. 创建两个并行任务，塞入它们各自的任务函数与独立的栈
    sched.create_task(task1, task1_stack, sizeof(task1_stack));
    sched.create_task(task2, task2_stack, sizeof(task2_stack));

    // 3. 设置初始的全局 TCB 指针，供首次切换使用
    g_current_tcb_ptr = sched.get_current_tcb();

    // 4. 配置并启动内核的心跳定时器 SysTick (每 1ms 触发一次中断)
    // 映射到地址 0xE000E010 启动系统滴答
    volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    volatile uint32_t* syst_load = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    *syst_load = (SYSCLK_FREQ / 1000) - 1; // 1ms 周期
    *syst_ctrl = (1 << 2) | (1 << 1) | (1 << 0); // 开启定时器与中断

    // 5. 将主栈指针 (MSP) 切换为线程栈指针 (PSP)，并大步跨入多任务世界
    __asm__ volatile (
        "msr psp, %0\n\t"       // 将任务 1 的初始栈顶给 PSP
        "mov r0, #2\n\t"
        "msr control, r0\n\t"   // CONTROL 寄存器写 2：强制 CPU 切换到 PSP 栈运行
        "isb\n\t"               // 清空指令流水线
        "cpsie i\n\t"           // 全局开中断！定时器开始数数
        "bl task1\n\t"          // 首个任务直接就地起飞
        :
        : "r"(g_current_tcb_ptr->stack_ptr)
        : "r0", "memory"
    );

    while (1) {}
}

