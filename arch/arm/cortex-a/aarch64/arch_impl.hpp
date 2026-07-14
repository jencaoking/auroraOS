#ifndef ARCH_AARCH64_IMPL_HPP
#define ARCH_AARCH64_IMPL_HPP

#include <stdint.h>

namespace Arch {
    inline void disable_interrupts() {
        __asm__ volatile ("msr daifset, #2" : : : "memory");
    }

    inline void enable_interrupts() {
        __asm__ volatile ("msr daifclr, #2" : : : "memory");
    }

    inline uint32_t irq_save() {
        uint32_t flags;
        __asm__ volatile (
            "mrs %0, daif \n\t"
            "msr daifset, #2 \n\t"
            : "=r" (flags)
            :
            : "memory"
        );
        return flags;
    }

    inline void irq_restore(uint32_t flags) {
        __asm__ volatile (
            "msr daif, %0 \n\t"
            :
            : "r" (flags)
            : "memory"
        );
    }

    inline void wait_for_interrupt() {
        __asm__ volatile ("wfi" : : : "memory");
    }

    inline uint32_t get_cycle() {
        uint64_t val;
        __asm__ volatile ("mrs %0, cntvct_el0" : "=r" (val));
        return static_cast<uint32_t>(val);
    }

    inline void trigger_context_switch() {
        __asm__ volatile ("svc #0" : : : "memory");
    }
}

#endif // ARCH_AARCH64_IMPL_HPP
