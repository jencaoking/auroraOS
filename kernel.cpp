#include "uart.h"
#include "config.h"
#include "interrupts.hpp"
#include "task.hpp"
#include "memory.hpp"
#include "softbus.hpp"
#include "mutex.hpp"
#include "msg_queue.hpp"

extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

Mutex uart_mutex;
void safe_print(const char* msg) {
    uart_mutex.lock();
    uart_puts(msg);
    uart_mutex.unlock();
}

// 1. 定义消息载体结构
struct RpcMessage {
    char payload[64];
    
    // 裸机环境下简单的深拷贝赋值函数
    void set_payload(const char* p) {
        int i = 0;
        while (p[i] && i < 63) { payload[i] = p[i]; i++; }
        payload[i] = '\0';
    }
};

// 2. 实例化全局消息队列（容量为 8）
MessageQueue<RpcMessage, 8> app_queue;

extern "C" void bus_daemon_task(void) {
    while (1) {
        SoftBus::instance().poll();
        Scheduler::instance().sleep(10);
    }
}

// 3. 业务线程彻底变为“事件驱动”模型
extern "C" void app_task(void) {
    while (1) {
        // pop() 会挂起当前线程，直到队列里被推入了新的消息。
        // 这意味着平时 app_task 的 CPU 占用率为 0%！
        RpcMessage msg = app_queue.pop();
        
        safe_print("\n[App Task] Woke up! Received RPC Payload: ");
        safe_print(msg.payload);
        safe_print("\n[App Task] Processing heavy workload for 1 second...\n");
        
        // 模拟极其耗时的业务处理
        Scheduler::instance().sleep(1000); 
        
        safe_print("[App Task] Workload complete. Going back to sleep.\n\n");
    }
}

extern "C" void kernel_main(void) {
    uart_init();
    KernelHeap::instance().init(&_heap_start, &_heap_end);
    SoftBus::instance().init();
    app_queue.init();

    // 4. 重构微服务回调：守护线程现在只负责 Push 消息，绝不恋战
    SoftBus::instance().register_service("EXEC", [](const char* payload) {
        safe_print(">> [Daemon] 'EXEC' RPC matched. Pushing to App Queue...\n");
        
        RpcMessage msg;
        msg.set_payload(payload);
        app_queue.push(msg); // 瞬间完成，立刻返回继续监听串口
    });

    safe_print("\n========================================\n");
    safe_print(" auroraOS Upgraded: IPC Message Queue\n");
    safe_print("========================================\n");

    Scheduler& sched = Scheduler::instance();
    sched.init();

    uint32_t* daemon_stack = new uint32_t[256];
    uint32_t* app_stack = new uint32_t[256];
    
    sched.create_task(bus_daemon_task, daemon_stack, 256 * sizeof(uint32_t));
    sched.create_task(app_task, app_stack, 256 * sizeof(uint32_t));

    g_current_tcb_ptr = sched.get_current_tcb();

    volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    volatile uint32_t* syst_load = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    *syst_load = (SYSCLK_FREQ / 1000) - 1;
    *syst_ctrl = (1 << 2) | (1 << 1) | (1 << 0);

    __asm__ volatile (
        "msr psp, %0\n\t"
        "mov r0, #2\n\t"
        "msr control, r0\n\t"
        "isb\n\t"
        "cpsie i\n\t"
        "bl bus_daemon_task\n\t"
        : : "r"(g_current_tcb_ptr->stack_ptr) : "r0", "memory"
    );

    while (1) {}
}
