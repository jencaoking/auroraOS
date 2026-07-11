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
}

#endif // ARCH_IMPL_HPP
