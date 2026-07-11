#ifndef ARCH_API_HPP
#define ARCH_API_HPP

#include <stdint.h>

// =====================================================================
// HAL 架构无关接口契约
// 调度器与内核逻辑层只依赖此处的声明，不感知具体异常返回码、寄存器魔数
// 或内联汇编。各架构在 arch_impl.hpp 中提供 inline 实现，构建系统按
// -DBOARD 自动将其加入 include 路径。
// =====================================================================
namespace Arch {
    // ── 临界区 / 低功耗 ────────────────────────────────────────────
    void disable_interrupts();
    void enable_interrupts();
    void wait_for_interrupt();

    // ── 上下文切换 ────────────────────────────────────────────────
    // 触发软中断请求调度器做硬件级上下文切换 (Cortex-M 上即 pending PendSV)
    void trigger_context_switch();

    // 伪造硬件异常压栈的初始线程栈帧，返回新的栈顶指针
    uint32_t* init_thread_stack(void (*task_entry)(void),
                                uint32_t* stack_space,
                                uint32_t stack_size);

    // 从特权 main 上下文引导进入第一个任务：切换 PSP/CONTROL、开中断、跳入入口
    // 与 init_thread_stack() 配套，是调度器唯一「无中生有」建立任务上下文的入口
    [[noreturn]] void start_first_task(uint32_t* stack_ptr,
                                       void (*entry_point)());
}

// 拉入当前架构的内联实现 (arch/arm/cortex-m/cm4/arch_impl.hpp 等)
#include "arch_impl.hpp"

#endif // ARCH_API_HPP
