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
#include "../net/eth_driver.hpp"

extern void safe_print(const char* msg);
extern Mutex uart_mutex;

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/api.h"

extern err_t ethernetif_init(struct netif *netif);
extern void ethernetif_input_task(void);

struct netif g_netif;

// ==========================================
// 业务层测试：基于 lwIP Netconn API 的 UDP Echo 服务器
// ==========================================
void udp_echo_server_task(void) {
    sys_print("[App] Starting UDP Echo Server on Port 8080...\r\n");

    // 创建一个 UDP 通信绑定
    struct netconn* conn = netconn_new(NETCONN_UDP);
    netconn_bind(conn, IP_ADDR_ANY, 8080);

    struct netbuf* buf;
    while (true) {
        // 挂起自身，直到物理网线那边发来目标为 8080 端口的数据包！
        if (netconn_recv(conn, &buf) == ERR_OK) {
            sys_print("\r\n>>> [UDP Server] Received packet from Net! Echoing back...\r\n");
            
            // 工业级原路奉还 (Echo 回发给源 IP 和源端口)
            netconn_sendto(conn, buf, netbuf_fromaddr(buf), netbuf_fromport(buf));
            netbuf_delete(buf);
        }
    }
}

// 协议栈就绪回调函数
void tcpip_init_done(void* arg) {
    sys_print("[lwIP] TCP/IP Core Stack Booted Successfully!\r\n");

    // 1. 设置静态 IPv4 地址: 192.168.1.100, 子网掩码: 255.255.255.0, 网关: 192.168.1.1
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);

    // 2. 将网卡挂载进 lwIP 路由表
    netif_add(&g_netif, &ipaddr, &netmask, &gw, nullptr, ethernetif_init, tcpip_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    sys_print("[lwIP] Static IP configured: 192.168.1.100\r\n");

    // 3. 起飞网卡数据泵任务与 UDP Echo 测试任务
    uint32_t* rx_stack = new uint32_t[512];
    uint32_t* app_stack = new uint32_t[512];
    Scheduler::instance().create_task(reinterpret_cast<void (*)()>(ethernetif_input_task), rx_stack, 512 * sizeof(uint32_t), true);
    Scheduler::instance().create_task(udp_echo_server_task, app_stack, 512 * sizeof(uint32_t), true);
}

extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

Mutex uart_mutex;

// 依然保留挂载设备
extern class UartDevice g_uart_device;

void dummy_task(void) {
    while (1) {
        // M4: Idle task must never block via software (sys_sleep),
        // but it SHOULD yield the hardware to low-power state.
        __asm__ volatile ("wfi");
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
        0x1a, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        // --- 3. 真实机器码 payload (26 Bytes) ---
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
    Scheduler::instance().init();

    uint32_t* shell_stack = new uint32_t[512];

    extern void shell_task(void);
    Scheduler::instance().create_task(shell_task, shell_stack, 512 * sizeof(uint32_t), false);

    // 调起 lwIP 协议栈引擎，传递就绪回调
    sys_print("[lwIP] Initializing TCP/IP Engine...\r\n");
    tcpip_init(tcpip_init_done, nullptr);

    uint32_t* dummy_stack = new uint32_t[128];
    Scheduler::instance().create_task(dummy_task, dummy_stack, 128 * sizeof(uint32_t), true);

    g_current_tcb_ptr = Scheduler::instance().get_current_tcb();

    volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    volatile uint32_t* syst_load = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    *syst_load = (SYSCLK_FREQ / 1000) - 1;
    *syst_ctrl = (1 << 2) | (1 << 1) | (1 << 0);

    __asm__ volatile (
        "msr psp, %0\n\t"
        "mov r0, #2\n\t"  // 2 = b10: Privileged, use PSP
        "msr control, r0\n\t"
        "isb\n\t"
        "cpsie i\n\t"
        "bl shell_task\n\t"
        : : "r"(g_current_tcb_ptr->stack_ptr) : "r0", "memory"
    );

    while (1) {}
}
