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
};

using InterruptHandler = void (*)(InterruptFrame*);

class InterruptManager {
public:
    static InterruptManager& instance();

    void init();
    void register_handler(int irq, InterruptHandler handler);
    void unregister_handler(int irq);
    void handle(InterruptFrame* frame);

private:
    InterruptManager() = default;
    static constexpr int MAX_IRQ = 64;
    InterruptHandler handlers_[MAX_IRQ] = {};
};

extern "C" {
    void SVC_Handler(InterruptFrame* frame);
    void PendSV_Handler();
    void SysTick_Handler();
}

#endif
