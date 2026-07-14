#ifndef INTERRUPTS_HPP
#define INTERRUPTS_HPP

#include <stdint.h>

struct InterruptFrame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
#if defined(ARCH_RISCV32)
    uint32_t svc_num;
#endif
};



extern "C" {
    void PendSV_Handler();
    void SysTick_Handler();
    void SVC_Handler_C(InterruptFrame* frame); // 由 boot.S 中的 SVC_Handler 汇编调用
}

#endif
