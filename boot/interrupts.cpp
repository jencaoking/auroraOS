#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"
#include "syscall.hpp"
#include "timer.hpp"
#include "work_queue.hpp"
#include "mpu.hpp"
#include "../kernel/cspace.hpp"
#include "../kernel/ipc.hpp"
#include "frame_scheduler_v2.hpp"
#include "../metrics/metrics.hpp"

volatile uint32_t isr_enter_cycle = 0;

// ────────────────────────────────────────────────────────────────
// SyscallValidator — kernel-side parameter validation for SVC calls.
// All functions are noexcept; they never throw or call user code.
// ────────────────────────────────────────────────────────────────
namespace SyscallValidator {

// Flash region boundaries exposed by the linker script.
// Defined as weak to allow host-test stubs to override them.
extern "C" __attribute__((weak)) uint32_t _flash_start;
extern "C" __attribute__((weak)) uint32_t _flash_end;

// Validate that [ptr, ptr+len) lies entirely within either:
//   (a) the calling task’s stack region, or
//   (b) the read-only Flash segment (for string literals).
// Returns true if the range is safe to dereference in kernel context.
[[nodiscard]] inline bool validate_user_ptr(
        const void* ptr, size_t len,
        uintptr_t task_stack_base, size_t task_stack_size) noexcept
{
    if (!ptr) return false;
    const uintptr_t p   = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end = p + len;
    // Integer wrap-around check
    if (end < p) return false;

    // (a) Within the task’s own stack
    if (task_stack_size > 0) {
        const bool in_stack = (p >= task_stack_base) &&
                              (end <= task_stack_base + task_stack_size);
        if (in_stack) return true;
    }

    // (b) Within read-only Flash (for string literals passed as SYS_PRINT arg)
    const uintptr_t flash_s = reinterpret_cast<uintptr_t>(&_flash_start);
    const uintptr_t flash_e = reinterpret_cast<uintptr_t>(&_flash_end);
    if (flash_s != flash_e) {  // linker symbols valid
        const bool in_flash = (p >= flash_s) && (end <= flash_e);
        if (in_flash) return true;
    }

    return false;
}

}  // namespace SyscallValidator


// 供 PendSV 汇编读取的两个全局 TCB 指针
// 声明为非 volatile：汇编直接使用符号地址，编译器临界区内通过 Arch:: 保护
extern "C" {
    TaskControlBlock* volatile g_current_tcb_ptr = nullptr;
    TaskControlBlock* volatile g_next_tcb_ptr    = nullptr;
    volatile uint32_t g_switch_start_cycle = 0;

    // 由 PendSV_Handler 调用的 MPU 动态沙盒切换
    void mpu_switch_sandbox(TaskControlBlock* next) {
        if (next && next->size_pow2 > 0) {
            MPU::instance().update_user_sandbox_verified(next->mpu_sandbox);
        }
    }

    void pendsv_metrics_hook() {
        if (g_switch_start_cycle > 0) {
            Metrics::record(METRIC_CTX_SWITCH, Arch::get_cycle() - g_switch_start_cycle);
            g_switch_start_cycle = 0;
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
        isr_enter_cycle = Arch::get_cycle();
#if defined(ARCH_RISCV32)
        const uint8_t svc_number = static_cast<uint8_t>(frame->svc_num);
#else
        // 通过 PC 回溯到 SVC 指令，提取 8 位系统调用号
        const uint16_t svc_instr = reinterpret_cast<const uint16_t*>(frame->pc)[-1];
        const uint8_t  svc_number = static_cast<uint8_t>(svc_instr & 0xFF);
#endif

        // 获取当前任务的栈边界，用于参数指针校验
        const TaskControlBlock* cur = Scheduler::instance().get_current_tcb();
        const uintptr_t stack_base  = cur ? static_cast<uintptr_t>(cur->stack_base) : 0u;
        const size_t    stack_size  =
            cur ? (static_cast<size_t>(1) << cur->size_pow2) : 0u;

        switch (svc_number) {
            case SYS_PRINT: { // SysCall: 串口输出（在内核特权态安全调用）
                // 【参数校验】：r0 指针必须属于该任务的栈或 Flash 只读段
                // 字符串最长限制 256 字节（防止无界读）
                constexpr size_t MAX_PRINT_LEN = 256u;
                const char* str = reinterpret_cast<const char*>(frame->r0);
                if (!SyscallValidator::validate_user_ptr(
                        str, MAX_PRINT_LEN, stack_base, stack_size)) {
                    uart_puts("[Kernel] SYS_PRINT: invalid ptr rejected\n");
                    break;
                }
                uart_puts(str);
                break;
            }
            case SYS_YIELD: // SysCall: 任务 Yield
                Scheduler::instance().schedule();
                break;
            case SYS_SLEEP: { // SysCall: 任务 Sleep
                // 【参数校验】：sleep 时长不超过 10 分钟（600,000 ms）
                constexpr uint32_t MAX_SLEEP_MS = 600'000u;
                const uint32_t ms = frame->r0;
                if (ms > MAX_SLEEP_MS) {
                    uart_puts("[Kernel] SYS_SLEEP: duration out of range\n");
                    break;
                }
                Scheduler::instance().sleep_ms(ms);
                break;
            }
            case SYS_IPC_CALL: { // SysCall: 发起 IPC 请求并阻塞等待响应
                if (!cur) break;
                uint32_t cap_id = frame->r0;
                void* msg = reinterpret_cast<void*>(frame->r1);
                uint32_t len = frame->r2;
                void* reply_buf = reinterpret_cast<void*>(frame->r3);
                // Note: In inline asm we pushed 5th arg to stack (or r4), for simplicity here we assume 5th arg is in r12 or stack, but since we didn't push properly in SVC frame, let's hardcode max_reply_len for safety in phase 1 or fetch from r4 if we saved it. 
                // Cortex-M exception frame doesn't push r4. Let's assume max_reply_len is 256.
                uint32_t max_reply_len = 256; 
                
                if (cap_id < auroraos::kernel::MAX_CSPACE_SLOTS) {
                    const auto& cap = cur->cspace[cap_id];
                    if (cap.type == auroraos::kernel::CapType::Endpoint && cap.rights.write) {
                        auto* ep = static_cast<auroraos::kernel::Endpoint*>(cap.object);
                        // We must cast away const to call Endpoint methods (scheduler tasks are mutated)
                        TaskControlBlock* mutable_cur = Scheduler::instance().get_current_tcb();
                        ep->call(mutable_cur, msg, len, reply_buf, max_reply_len);
                        Scheduler::instance().schedule(); // Block and switch task
                    }
                }
                break;
            }
            case SYS_IPC_RECEIVE: { // SysCall: 接收 IPC 请求
                if (!cur) break;
                uint32_t cap_id = frame->r0;
                void* msg_buf = reinterpret_cast<void*>(frame->r1);
                uint32_t max_len = frame->r2;
                uint32_t* out_sender_id = reinterpret_cast<uint32_t*>(frame->r3);
                
                if (cap_id < auroraos::kernel::MAX_CSPACE_SLOTS) {
                    const auto& cap = cur->cspace[cap_id];
                    if (cap.type == auroraos::kernel::CapType::Endpoint && cap.rights.read) {
                        auto* ep = static_cast<auroraos::kernel::Endpoint*>(cap.object);
                        TaskControlBlock* mutable_cur = Scheduler::instance().get_current_tcb();
                        ep->receive(mutable_cur, msg_buf, max_len);
                        
                        if (mutable_cur->ipc_state == auroraos::kernel::IpcState::Receiving) {
                            // No sender was waiting, we blocked.
                            Scheduler::instance().schedule();
                        } else if (out_sender_id) {
                            // We fast-pathed and received a message immediately.
                            *out_sender_id = mutable_cur->ipc_sender_id;
                        }
                    }
                }
                break;
            }
            case SYS_IPC_REPLY: { // SysCall: 回复 IPC 请求
                if (!cur) break;
                uint32_t sender_id = frame->r0;
                void* reply_msg = reinterpret_cast<void*>(frame->r1);
                uint32_t len = frame->r2;
                // Reply is non-blocking for the receiver, it just wakes up the sender.
                // We use static reply method for now
                auroraos::kernel::Endpoint::reply(nullptr, sender_id, reply_msg, len);
                Scheduler::instance().schedule(); // Yield to the awoken sender (Fastpath)
                break;
            }
            default:
                uart_puts("[Kernel] Unknown SVC — possible exploit attempt\n");
                // 终止违规任务而非整个系统
                if (cur) {
                    Scheduler::instance().set_task_state(
                        cur->id, TaskState::Terminated);
                    Scheduler::instance().schedule();
                }
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
    isr_enter_cycle = Arch::get_cycle();
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
