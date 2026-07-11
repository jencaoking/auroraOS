#ifndef ARCH_API_HPP
#define ARCH_API_HPP

#include <stdint.h>
// 构建系统会根据 -DBOARD 自动包含对应架构的 arch_impl.hpp
#include "arch_impl.hpp"

namespace Arch {
    // 此处已由 arch_impl.hpp 实现以下标准 HAL API:
    // - void disable_interrupts();
    // - void enable_interrupts();
    // - void wait_for_interrupt();
    // - void trigger_context_switch();
    // - uint32_t* init_thread_stack(...);
}

#endif // ARCH_API_HPP
