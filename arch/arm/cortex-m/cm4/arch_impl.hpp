#ifndef ARCH_IMPL_HPP
#define ARCH_IMPL_HPP

#include <stdint.h>
#include "board.h"  // BOARD_SYSCLK_FREQ — 计算 SysTick 重载值

namespace Arch {
    // =====================================================================
    // ARM Cortex-M4 硬件架构魔数与标志位
    // =====================================================================
    constexpr uintptr_t ICSR_ADDR      = 0xE000ED04U;  // 中断控制及状态寄存器
    constexpr uint32_t  ICSR_PENDSVSET = (1UL << 28);  // 触发 PendSV 切换位
    constexpr uint32_t  EXC_RETURN_PSP = 0xFFFFFFFDU;  // 异常返回：使用 PSP 线程栈
    constexpr uint32_t  XPSR_THUMB     = 0x01000000U;  // Thumb 指令集状态位

    // ── SysTick 定时器寄存器 (ARMv7-M Reference Manual B3.3) ──────────
    constexpr uintptr_t SYST_CSR_ADDR  = 0xE000E010U;  // Control & Status
    constexpr uintptr_t SYST_RVR_ADDR  = 0xE000E014U;  // Reload Value
    constexpr uintptr_t SYST_CVR_ADDR  = 0xE000E015U;  // Current Value
    // SYST_CSR 位域: [2]=CLKSOURCE(1=CPU时钟) [1]=TICKINT [0]=ENABLE
    constexpr uint32_t  SYST_CSR_ENABLE    = (1UL << 0);
    constexpr uint32_t  SYST_CSR_TICKINT   = (1UL << 1);
    constexpr uint32_t  SYST_CSR_CLKSOURCE = (1UL << 2);

    // ── DWT 周期计数器寄存器 ──────────
    constexpr uintptr_t DEMCR_ADDR      = 0xE000EDFCU;
    constexpr uintptr_t DWT_CTRL_ADDR   = 0xE0001000U;
    constexpr uintptr_t DWT_CYCCNT_ADDR = 0xE0001004U;

    // =====================================================================
    // 底层内联汇编控制
    // =====================================================================
    inline void disable_interrupts() {
        __asm__ volatile ("cpsid i" : : : "memory");
    }

    inline void enable_interrupts() {
        __asm__ volatile ("cpsie i" : : : "memory");
    }

    inline uint32_t irq_save() {
        uint32_t flags;
        __asm__ volatile (
            "mrs %0, primask \n\t"
            "cpsid i         \n\t"
            : "=r" (flags)
            :
            : "memory"
        );
        return flags;
    }

    inline void irq_restore(uint32_t flags) {
        __asm__ volatile (
            "msr primask, %0 \n\t"
            :
            : "r" (flags)
            : "memory"
        );
    }

    inline void wait_for_interrupt() {
        __asm__ volatile ("wfi" : : : "memory");
    }

    inline void init_dwt() {
        *reinterpret_cast<volatile uint32_t*>(DEMCR_ADDR) |= (1UL << 24); // TRCENA
        *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT_ADDR) = 0;
        *reinterpret_cast<volatile uint32_t*>(DWT_CTRL_ADDR) |= 1;        // CYCCNTENA
    }

    inline uint32_t get_cycle() {
        return *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT_ADDR);
    }

    inline uint32_t get_cycles_per_us() {
        return BOARD_SYSCLK_FREQ / 1000000;
    }

    // =====================================================================
    // SysTick 初始化：配置周期性系统心跳定时器
    //
    // hz = 期望的每秒中断次数（如 1000 → 每 1ms 一次中断）
    // 重载值 = (CPU 主频 / hz) - 1
    //
    // 配置时序（安全要求：必须在全局开中断 cpsie i 之前调用）：
    //   1. 先写 CSR=0 禁用 SysTick，防止配置过程中误触发中断
    //   2. 写 RVR 设定重载周期
    //   3. 写 CVR=0 清零当前计数器（同时清除 COUNTFLAG）
    //   4. 写 CSR 启用：CLKSOURCE=CPU时钟 + TICKINT=开中断 + ENABLE=启动
    // =====================================================================
    inline void systick_init(uint32_t hz) {
        volatile uint32_t* syst_csr = reinterpret_cast<volatile uint32_t*>(SYST_CSR_ADDR);
        volatile uint32_t* syst_rvr = reinterpret_cast<volatile uint32_t*>(SYST_RVR_ADDR);
        volatile uint32_t* syst_cvr = reinterpret_cast<volatile uint32_t*>(SYST_CVR_ADDR);

        *syst_csr = 0;                                        // 1. 禁用 SysTick
        *syst_rvr = (BOARD_SYSCLK_FREQ / hz) - 1;           // 2. 设定重载周期
        *syst_cvr = 0;                                       // 3. 清零当前值
        *syst_csr = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE; // 4. 启动

        init_dwt();                                          // 5. 启动 DWT
    }

    inline void disable_systick() {
        volatile uint32_t* syst_csr = reinterpret_cast<volatile uint32_t*>(SYST_CSR_ADDR);
        *syst_csr &= ~SYST_CSR_ENABLE;
    }

    inline void enable_systick() {
        volatile uint32_t* syst_csr = reinterpret_cast<volatile uint32_t*>(SYST_CSR_ADDR);
        *syst_csr |= SYST_CSR_ENABLE;
    }

    // Tickless Idle: 唤醒定时器存根实现 (Stub)
    // 真实的硬件移植需要在具体的板级驱动中重写该实现 (如连接 RTC)
    inline void start_wakeup_timer(uint32_t /*ticks*/) {
        // 空实现：QEMU 测试环境可不接写真实 RTC
    }

    inline uint32_t stop_wakeup_timer() {
        // 返回0表示没有经过额外的时间，补偿由常规 SysTick 处理即可
        return 0;
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
