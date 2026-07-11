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
    // 1. 预设之前写的 log.txt
    int fd = VfsManager::instance().open("/tmp/log.txt");
    if (fd >= 0) {
        const char* secret = "Hello from auroraOS RamFS! You found the hidden message.";
        int len = 0; while (secret[len]) len++;
        VfsManager::instance().write(fd, secret, len);
        VfsManager::instance().close(fd);
    }

    // 2. 【极其硬核】我们在内存中手写构建一个真实的 100 字节 ARM Thumb 可执行 ELF 文件！
    // 它包含了一个正常的 Elf32_Ehdr, Elf32_Phdr 以及一段执行 SVC #0x01 系统调用的机器码！
    static const unsigned char mini_arm_elf[] = {
        // --- 1. Elf32_Ehdr (52 Bytes) ---
        0x7f, 'E', 'L', 'F', 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00, 0x01, 0x00, 0x28, 0x00,
        0x00, 0x00, 0x00, 0x00,
        // --- 2. Elf32_Phdr (32 Bytes) ---
        0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        // --- 3. 真实机器码 payload (24 Bytes) ---
        // 汇编意义：将 PC+4 处的数据字符串地址放入 R0，然后执行 SVC #0x01，最后死循环休眠
        0x01, 0xa0, 0x01, 0xdf, 0xfe, 0xe7, 0x00, 0x00, 
        // 字符串内容: "DYNAMIC ELF OK!"
        'D', 'Y', 'N', 'A', 'M', 'I', 'C', ' ', 'E', 'L', 'F', ' ', 'O', 'K', '!', '\r', '\n', '\0'
    };

    int elf_fd = VfsManager::instance().open("/tmp/app.elf");
    if (elf_fd >= 0) {
        VfsManager::instance().write(elf_fd, reinterpret_cast<const char*>(mini_arm_elf), sizeof(mini_arm_elf));
        VfsManager::instance().close(elf_fd);
    }

    // 3. 启动命令行终端
    Shell::run();
}

extern "C" void kernel_main(void) {
    uart_init();
    KernelHeap::instance().init(&_heap_start, &_heap_end);
    VfsManager::instance().init();

    RamFile* temp_file = new RamFile(1024);
    RamFile* elf_file = new RamFile(1024);

    // 挂载 /dev/tty0 和 /tmp/log.txt 以及 /tmp/app.elf
    VfsManager::instance().mount("/dev/tty0", (VNode*)&g_uart_device);
    VfsManager::instance().mount("/tmp/log.txt", (VNode*)temp_file);
    VfsManager::instance().mount("/tmp/app.elf", (VNode*)elf_file);
    
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
