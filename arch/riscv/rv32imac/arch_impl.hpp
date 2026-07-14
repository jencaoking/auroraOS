#ifndef ARCH_IMPL_HPP
#define ARCH_IMPL_HPP

#include <stdint.h>
#include "board.h"

namespace Arch {
    constexpr uintptr_t CLINT_BASE = BOARD_CLINT_BASE;
    constexpr uintptr_t CLINT_MSIP = CLINT_BASE + 0x0000;
    constexpr uintptr_t CLINT_MTIMECMP = CLINT_BASE + 0x4000;
    constexpr uintptr_t CLINT_MTIME  = CLINT_BASE + 0xBFF8;

    inline void disable_interrupts() {
        __asm__ volatile ("csrc mstatus, %0" : : "r"(8) : "memory");
    }

    inline void enable_interrupts() {
        __asm__ volatile ("csrs mstatus, %0" : : "r"(8) : "memory");
    }

    inline uint32_t irq_save() {
        uint32_t flags;
        __asm__ volatile (
            "csrr %0, mstatus \n\t"
            "csrc mstatus, %1"
            : "=r" (flags)
            : "r" (8)
            : "memory"
        );
        return flags;
    }

    inline void irq_restore(uint32_t flags) {
        __asm__ volatile (
            "csrw mstatus, %0"
            :
            : "r" (flags)
            : "memory"
        );
    }

    inline void wait_for_interrupt() {
        __asm__ volatile ("wfi" : : : "memory");
    }

    inline uint32_t get_cycle() {
        return *reinterpret_cast<volatile uint32_t*>(CLINT_MTIME);
    }

    inline uint32_t get_cycles_per_us() {
        // Typical QEMU virt CLINT frequency is 10MHz
        return 10;
    }

    inline void systick_init(uint32_t hz) {
        // Enable Machine Timer Interrupt (MTIE = bit 7 in mie)
        uint32_t mtie = (1 << 7);
        __asm__ volatile ("csrs mie, %0" : : "r"(mtie) : "memory");

        uint32_t now = get_cycle();
        uint32_t interval = (get_cycles_per_us() * 1000000) / hz;
        uint64_t target = static_cast<uint64_t>(now) + interval;
        
        volatile uint32_t* mtimecmp = reinterpret_cast<volatile uint32_t*>(CLINT_MTIMECMP);
        mtimecmp[0] = 0xFFFFFFFF; // Prevent spurious
        mtimecmp[1] = static_cast<uint32_t>(target >> 32);
        mtimecmp[0] = static_cast<uint32_t>(target & 0xFFFFFFFF);
    }

    inline void disable_systick() {
        uint32_t mtie = (1 << 7);
        __asm__ volatile ("csrc mie, %0" : : "r"(mtie) : "memory");
    }

    inline void enable_systick() {
        uint32_t mtie = (1 << 7);
        __asm__ volatile ("csrs mie, %0" : : "r"(mtie) : "memory");
    }

    inline void start_wakeup_timer(uint32_t ticks) {}
    inline uint32_t stop_wakeup_timer() { return 0; }

    inline void trigger_context_switch() {
        // Trigger Machine Software Interrupt
        *reinterpret_cast<volatile uint32_t*>(CLINT_MSIP) = 1;
    }

    inline uint32_t* init_thread_stack(void (*task_entry)(void), uint32_t* stack_space, uint32_t stack_size) {
        uint32_t* top = stack_space + (stack_size / sizeof(uint32_t));
        
        // Trap frame: 33 words (132 bytes)
        // [0..30] GPRs x1-x31
        // [31] mepc
        // [32] mstatus
        top -= 33;
        
        top[32] = 0x1880; // mstatus: MPP=3 (M-mode), MPIE=1
        top[31] = reinterpret_cast<uint32_t>(task_entry); // mepc
        
        // Zero init x1-x31
        for(int i = 0; i <= 30; i++) top[i] = 0;
        
        return top;
    }

    [[noreturn]] inline void start_first_task(uint32_t* stack_ptr, void (*entry_point)(), uint32_t privilege = 0) {
        __asm__ volatile (
            "mv sp, %0 \n\t"
            // Load mepc and mstatus
            "lw t0, 124(sp) \n\t"
            "csrw mepc, t0 \n\t"
            "lw t0, 128(sp) \n\t"
            "csrw mstatus, t0 \n\t"
            // Restore context GPRs (x1-x31)
            "lw x1, 0(sp) \n\t"
            "lw x3, 8(sp) \n\t"
            "lw x4, 12(sp) \n\t"
            "lw x5, 16(sp) \n\t"
            "lw x6, 20(sp) \n\t"
            "lw x7, 24(sp) \n\t"
            "lw x8, 28(sp) \n\t"
            "lw x9, 32(sp) \n\t"
            "lw x10, 36(sp) \n\t"
            "lw x11, 40(sp) \n\t"
            "lw x12, 44(sp) \n\t"
            "lw x13, 48(sp) \n\t"
            "lw x14, 52(sp) \n\t"
            "lw x15, 56(sp) \n\t"
            "lw x16, 60(sp) \n\t"
            "lw x17, 64(sp) \n\t"
            "lw x18, 68(sp) \n\t"
            "lw x19, 72(sp) \n\t"
            "lw x20, 76(sp) \n\t"
            "lw x21, 80(sp) \n\t"
            "lw x22, 84(sp) \n\t"
            "lw x23, 88(sp) \n\t"
            "lw x24, 92(sp) \n\t"
            "lw x25, 96(sp) \n\t"
            "lw x26, 100(sp) \n\t"
            "lw x27, 104(sp) \n\t"
            "lw x28, 108(sp) \n\t"
            "lw x29, 112(sp) \n\t"
            "lw x30, 116(sp) \n\t"
            "lw x31, 120(sp) \n\t"
            // Pop stack frame
            "addi sp, sp, 132 \n\t"
            "mret \n\t"
            : : "r"(stack_ptr)
        );
        __builtin_unreachable();
    }
}

#endif // ARCH_IMPL_HPP
