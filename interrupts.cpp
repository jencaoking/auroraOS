#include "interrupts.hpp"
#include "uart.h"
#include "task.hpp"

// 供汇编读取的两个全局 TCB 指针
extern "C" {
    TaskControlBlock* g_current_tcb_ptr = nullptr;
    TaskControlBlock* g_next_tcb_ptr = nullptr;
}

static uint32_t tick_count = 0;

InterruptManager& InterruptManager::instance()
{
    static InterruptManager mgr;
    return mgr;
}

void InterruptManager::init()
{
    for (int i = 0; i < MAX_IRQ; i++)
        handlers_[i] = nullptr;
}

void InterruptManager::register_handler(int irq, InterruptHandler handler)
{
    if (irq >= 0 && irq < MAX_IRQ)
        handlers_[irq] = handler;
}

void InterruptManager::unregister_handler(int irq)
{
    if (irq >= 0 && irq < MAX_IRQ)
        handlers_[irq] = nullptr;
}

void InterruptManager::handle(InterruptFrame* frame)
{
    (void)frame;
}

extern "C" {

void SVC_Handler(InterruptFrame* frame)
{
    InterruptManager::instance().handle(frame);
}

void SysTick_Handler(void)
{
    tick_count++;
    
    // 每 20ms 进行一次线程调度切换 (时间片轮转)
    if (tick_count % 20 == 0) {
        Scheduler& sched = Scheduler::instance();
        g_current_tcb_ptr = sched.get_current_tcb();
        
        sched.schedule(); // 改变内部任务索引，并挂起 PendSV
        
        g_next_tcb_ptr = sched.get_current_tcb();
    }
}

}

