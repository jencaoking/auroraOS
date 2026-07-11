#ifndef ARCH_IMPL_HPP
#define ARCH_IMPL_HPP

#include <stdint.h>

namespace Arch {
    // =====================================================================
    // ARM Cortex-M4 硬件架构魔数与标志位
    // =====================================================================
    constexpr uintptr_t ICSR_ADDR      = 0xE000ED04U;  // 中断控制及状态寄存器
    constexpr uint32_t  ICSR_PENDSVSET = (1UL << 28);  // 触发 PendSV 切换位
    constexpr uint32_t  EXC_RETURN_PSP = 0xFFFFFFFDU;  // 异常返回：使用 PSP 线程栈
    constexpr uint32_t  XPSR_THUMB     = 0x01000000U;  // Thumb 指令集状态位

    // =====================================================================
    // 底层内联汇编控制
    // =====================================================================
    inline void disable_interrupts() {
        __asm__ volatile ("cpsid i" : : : "memory");
    }

    inline void enable_interrupts() {
        __asm__ volatile ("cpsie i" : : : "memory");
    }

    inline void wait_for_interrupt() {
        __asm__ volatile ("wfi" : : : "memory");
    }

    inline void trigger_context_switch() {
        *reinterpret_cast<volatile uint32_t*>(ICSR_ADDR) = ICSR_PENDSVSET;
    }

    // =====================================================================
    // 硬件线程初始栈帧伪造 (模拟硬件中断压栈)
    // =====================================================================
    inline uint32_t* init_thread_stack(void (*task_entry)(void), uint32_t* stack_space, uint32_t stack_size) {
        uint32_t* top = stack_space + (stack_size / sizeof(uint32_t));

        top--; *top = XPSR_THUMB;                             // xPSR
        top--; *top = reinterpret_cast<uint32_t>(task_entry); // PC (入口地址)
        top--; *top = EXC_RETURN_PSP;                         // LR
        top -= 5;                                             // R12, R3, R2, R1, R0
        top -= 8;                                             // R11 ~ R4 (汇编中手动弹出)

        return top;
    }

    // =====================================================================
    // 引导第一个任务：从特权 main 栈切换到线程 PSP 栈，恢复首个任务上下文
    // 与 init_thread_stack() 配套：弹出预留的 R4-R11，置 PSP/CONTROL，开中断后跳入入口
    // 这是调度器与具体架构异常返回机制之间唯一的接触面，移植时只需重写此函数
    // =====================================================================
    [[noreturn]] inline void start_first_task(uint32_t* stack_ptr, void (*entry_point)()) {
        __asm__ volatile (
            "ldm  %0!, {r4-r11}  \n\t"  // 弹出 R4-R11（init_thread_stack 预留的）
            "msr  psp, %0        \n\t"  // 将更新后的指针写入 PSP
            "mov  r0, #2         \n\t"  // CONTROL = 0b10: Thread mode, use PSP
            "msr  control, r0   \n\t"
            "isb                 \n\t"  // 指令同步屏障
            "cpsie i             \n\t"  // 全局开中断
            "bx   %1             \n\t"  // 跳入任务入口（直接 bx，不保存 LR）
            : : "r"(stack_ptr),
                "r"(reinterpret_cast<uint32_t>(entry_point))
            : "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "memory"
        );
        __builtin_unreachable();
    }
}

#endif // ARCH_IMPL_HPP
