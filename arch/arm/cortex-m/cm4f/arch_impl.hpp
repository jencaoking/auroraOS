#ifndef AURORA_ARCH_CM4F_IMPL_HPP
#define AURORA_ARCH_CM4F_IMPL_HPP

#include <stdint.h>
#include "board.h" // 包含 SYSTEM_CORE_CLOCK

// ========================================================
// Cortex-M4F 核心控制寄存器定义
// ========================================================
#define SCB_CPACR       (*((volatile uint32_t *)0xE000ED88)) // 协处理器访问控制寄存器
#define SCB_ICSR        (*((volatile uint32_t *)0xE000ED04)) // 中断控制和状态寄存器
#define SHPR3_PRI_14    (*((volatile uint8_t  *)0xE000ED22)) // PendSV 优先级寄存器
#define SHPR3_PRI_15    (*((volatile uint8_t  *)0xE000ED23)) // SysTick 优先级寄存器

namespace Arch {

    // ========================================================
    // 开启 M4F 硬件浮点运算单元 (FPU)
    // ========================================================
    inline void enable_fpu() {
        // 设置 CP10 和 CP11 协处理器的访问权限为全权访问 (Full Access)
        // 这将允许执行所有的硬件浮点指令
        SCB_CPACR |= (0xF << 20);
        __asm__ volatile ("dsb\n\t" "isb\n\t" : : : "memory");
    }

    // ========================================================
    // 初始化系统架构级中断与调度环境
    // ========================================================
    inline void init() {
        enable_fpu();

        // 将 PendSV 和 SysTick 的中断优先级设置为最低 (0xFF)
        // 确保系统的上下文切换只在没有其他 high-priority 硬件中断抢占时才发生
        SHPR3_PRI_14 = 0xFF;
        SHPR3_PRI_15 = 0xFF;
    }

    // ========================================================
    // 底层内联汇编控制
    // ========================================================
    inline void disable_interrupts() {
        __asm__ volatile ("cpsid i" : : : "memory");
    }

    inline void enable_interrupts() {
        __asm__ volatile ("cpsie i" : : : "memory");
    }

    inline void wait_for_interrupt() {
        __asm__ volatile ("wfi" : : : "memory");
    }

    // ========================================================
    // 触发软中断，请求上下文切换
    // ========================================================
    inline void trigger_context_switch() {
        SCB_ICSR |= (1 << 28); // 触发 PendSV 中断
        __asm__ volatile ("isb\n\t" "dsb\n\t" : : : "memory");
    }

    // ========================================================
    // M4F 任务栈初始化构造器
    // ========================================================
    // 为新创建的线程伪造一个中断返回时的堆栈快照。
    // 对于 M4F 架构，除了 R0-R12, LR, PC, xPSR 外，如果硬件使用了 FPU 延迟压栈，
    // EXC_RETURN 的值必须是 0xFFFFFFFD (不带FPU压栈) 或 0xFFFFFFED (带FPU压栈)。
    // 这里初始创建任务时，我们按照标准线程模式(Thread Mode + PSP + 无初始FPU快照)配置。
    inline uint32_t* init_task_stack(uint32_t* stack_top, void (*entry_point)(void)) {
        uint32_t* stk = stack_top;

        // 模拟硬件自动压栈的上下文 (Exception Frame)
        stk--; *stk = 0x01000000;                      // xPSR: 设置 Thumb 位 (T-bit)
        stk--; *stk = reinterpret_cast<uint32_t>(entry_point); // PC: 任务入口地址
        stk--; *stk = 0xFFFFFFFD;                      // LR (R14): 设为 EXC_RETURN，表明返回后使用 PSP，并且初始不包含扩展的 FPU 栈帧
        stk--; *stk = 0x12121212;                      // R12
        stk--; *stk = 0x03030303;                      // R3
        stk--; *stk = 0x02020202;                      // R2
        stk--; *stk = 0x01010101;                      // R1
        stk--; *stk = 0x00000000;                      // R0: 传参可以放这里

        // 模拟软件手动压栈的上下文 (被调用者保存寄存器)
        stk--; *stk = 0x11111111;                      // R11
        stk--; *stk = 0x10101010;                      // R10
        stk--; *stk = 0x09090909;                      // R9
        stk--; *stk = 0x08080808;                      // R8
        stk--; *stk = 0x07070707;                      // R7
        stk--; *stk = 0x06060606;                      // R6
        stk--; *stk = 0x05050505;                      // R5
        stk--; *stk = 0x04040404;                      // R4

        // 最终返回当前的栈顶指针，存入 TCB 中
        return stk;
    }

    inline uint32_t* init_thread_stack(void (*task_entry)(void), uint32_t* stack_space, uint32_t stack_size) {
        uint32_t* top = stack_space + (stack_size / sizeof(uint32_t));
        return init_task_stack(top, task_entry);
    }

    // ========================================================
    // SysTick 初始化
    // ========================================================
    inline void systick_init(uint32_t hz) {
        volatile uint32_t* syst_csr = reinterpret_cast<volatile uint32_t*>(0xE000E010);
        volatile uint32_t* syst_rvr = reinterpret_cast<volatile uint32_t*>(0xE000E014);
        volatile uint32_t* syst_cvr = reinterpret_cast<volatile uint32_t*>(0xE000E018);

        *syst_csr = 0;                                        // 1. 禁用 SysTick
        *syst_rvr = (SYSTEM_CORE_CLOCK / hz) - 1;             // 2. 设定重载周期
        *syst_cvr = 0;                                       // 3. 清零当前值
        *syst_csr = (1UL << 2) | (1UL << 1) | (1UL << 0);    // 4. 启动 (CLKSOURCE | TICKINT | ENABLE)
    }

    // ========================================================
    // 引导第一个任务
    // ========================================================
    [[noreturn]] inline void start_first_task(uint32_t* stack_ptr, void (*entry_point)()) {
        __asm__ volatile (
            "ldm  %0!, {r4-r11}  \n\t"  // 弹出 R4-R11
            "msr  psp, %0        \n\t"  // 将更新后的指针写入 PSP
            "mov  r0, #2         \n\t"  // CONTROL = 0b10: Thread mode, use PSP
            "msr  control, r0   \n\t"
            "isb                 \n\t"  // 指令同步屏障
            "cpsie i             \n\t"  // 全局开中断
            "bx   %1             \n\t"  // 跳入任务入口
            : : "r"(stack_ptr),
                "r"(reinterpret_cast<uint32_t>(entry_point))
            : "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "memory"
        );
        __builtin_unreachable();
    }

} // namespace Arch

#endif // AURORA_ARCH_CM4F_IMPL_HPP
