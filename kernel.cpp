#include "uart.h"
#include "config.h"
#include "interrupts.hpp"
#include "task.hpp"
#include "memory.hpp"

// 声明链接脚本定义的符号
extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

// 专门用来测试动态构建的 C++ 类
class DummyComponent {
private:
    uint32_t id;
public:
    DummyComponent(uint32_t val) : id(val) {
        uart_puts("    [C++ Constructor]: DummyComponent instance allocated via 'new'!\n");
    }
    void do_work() {
        uart_puts("    [C++ Method]: Method invoked dynamically via Heap object.\n");
    }
};

extern "C" void kernel_main(void) {
    uart_init();
    
    uart_puts("\n========================================\n");
    uart_puts("   auroraOS Memory Init -- Heap Testing\n");
    uart_puts("========================================\n");

    // 1. 初始化内核堆
    KernelHeap::instance().init(&_heap_start, &_heap_end);
    uart_puts("[Kernel Memory]: Kernel Heap Manager linked successfully.\n");

    // 2. 【核心测试】在裸机里大步执行标准 C++ 动态分配
    uart_puts("[Kernel Test]: Requesting dynamic object...\n");
    DummyComponent* my_comp = new DummyComponent(42);
    my_comp->do_work();

    // 3. 【核心测试】用完后手动安全销毁
    delete my_comp;
    uart_puts("[Kernel Test]: Object safely deleted via 'delete'.\n\n");

    // 4. 【多任务升级】现在连任务栈都可以不用静态死分配了，直接动态 new 出来！
    Scheduler& sched = Scheduler::instance();
    sched.init();

    uint32_t* task1_dyn_stack = new uint32_t[256];
    uint32_t* task2_dyn_stack = new uint32_t[256];

    // 外部定义的任务 task1 和 task2 逻辑保持不变（见上一步代码）
    extern void task1(void);
    extern void task2(void);
    
    sched.create_task(task1, task1_dyn_stack, 256 * sizeof(uint32_t));
    sched.create_task(task2, task2_dyn_stack, 256 * sizeof(uint32_t));

    g_current_tcb_ptr = sched.get_current_tcb();

    // 启动系统滴答时钟 SysTick（保持原样）...
    volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    volatile uint32_t* syst_load = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    *syst_load = (SYSCLK_FREQ / 1000) - 1;
    *syst_ctrl = (1 << 2) | (1 << 1) | (1 << 0);

    // 汇编切入多任务（保持原样）...
    __asm__ volatile (
        "msr psp, %0\n\t"
        "mov r0, #2\n\t"
        "msr control, r0\n\t"
        "isb\n\t"
        "cpsie i\n\t"
        "bl task1\n\t"
        : : "r"(g_current_tcb_ptr->stack_ptr) : "r0", "memory"
    );

    while (1) {}
}

// 任务 1：不断输出 A
extern "C" void task1(void) {
    while (1) {
        uart_putc('A');
        for (volatile int i = 0; i < 50000; i++); 
    }
}

// 任务 2：不断输出 B
extern "C" void task2(void) {
    while (1) {
        uart_putc('B');
        for (volatile int i = 0; i < 50000; i++);
    }
}
