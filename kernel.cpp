#include "uart.h"
#include "config.h"
#include "interrupts.hpp"
#include "task.hpp"
#include "memory.hpp"
#include "vfs.hpp"
#include "ramfs.hpp"
#include "shell.hpp" // 引入 Shell
#include "syscall.hpp"
#include "mutex.hpp"

extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

Mutex uart_mutex;

// 依然保留挂载设备
extern class UartDevice g_uart_device;

void dummy_task(void) {
    while (1) {
        // Idle task must never sleep. It just spins to keep CPU busy when others sleep.
    }
}

extern "C" void shell_task(void) {
    // 【隔离验证测试】
    // 尝试在非特权态访问系统控制空间 (SCB) 的 ICSR 寄存器以触发 PendSV
    // 如果隔离生效，这行代码会立即触发 BusFault 或 HardFault 异常
    volatile uint32_t* scb_icsr = (volatile uint32_t*)0xE000ED04;
    *scb_icsr = (1 << 28); 
    
    int fd = VfsManager::instance().open("/tmp/log.txt");
    if (fd >= 0) {
        const char* secret = "Hello from auroraOS RamFS! You found the hidden message.";
        int len = 0; while (secret[len]) len++;
        VfsManager::instance().write(fd, secret, len);
        VfsManager::instance().close(fd);
    }

    // 夺取前台控制权，启动命令行
    Shell::run();
}

extern "C" void kernel_main(void) {
    uart_init();
    KernelHeap::instance().init(&_heap_start, &_heap_end);
    VfsManager::instance().init();

    RamFile* temp_file = new RamFile(1024);

    // 挂载 /dev/tty0 和 /tmp/log.txt
    VfsManager::instance().mount("/dev/tty0", (VNode*)&g_uart_device);
    VfsManager::instance().mount("/tmp/log.txt", (VNode*)temp_file);
    
    // 初始化调度器并起飞
    Scheduler& sched = Scheduler::instance();
    sched.init();

    uint32_t* shell_stack = new uint32_t[512]; // 稍微给大一点栈空间
    // 【修改点】创建任务时，指定 is_privileged = false
    sched.create_task(shell_task, shell_stack, 512 * sizeof(uint32_t), false);

    uint32_t* dummy_stack = new uint32_t[128];
    sched.create_task(dummy_task, dummy_stack, 128 * sizeof(uint32_t), false);

    g_current_tcb_ptr = sched.get_current_tcb();

    volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    volatile uint32_t* syst_load = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    *syst_load = (SYSCLK_FREQ / 1000) - 1;
    *syst_ctrl = (1 << 2) | (1 << 1) | (1 << 0);

    __asm__ volatile (
        "msr psp, %0\n\t"
        "mov r0, #3\n\t"
        "msr control, r0\n\t"
        "isb\n\t"
        "cpsie i\n\t"
        "bl shell_task\n\t"
        : : "r"(g_current_tcb_ptr->stack_ptr) : "r0", "memory"
    );

    while (1) {}
}
