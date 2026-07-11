#include "uart.h"
#include "config.h"
#include "net_config.h"
#include "interrupts.hpp"
#include "task.hpp"
#include "memory.hpp"
#include "vfs.hpp"
#include "device.hpp"
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
#include "arch_api.hpp"

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

    // 1. 从配置层读取 IPv4 参数（IP/掩码/网关统一由 net_config.h 提供）
    const NetIpv4Config cfg = default_ipv4_config;
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr,  cfg.ip[0],      cfg.ip[1],      cfg.ip[2],      cfg.ip[3]);
    IP4_ADDR(&netmask, cfg.netmask[0], cfg.netmask[1], cfg.netmask[2], cfg.netmask[3]);
    IP4_ADDR(&gw,      cfg.gateway[0], cfg.gateway[1], cfg.gateway[2], cfg.gateway[3]);

    if (cfg.use_dhcp) {
        // 预留 DHCP 接入点：在 lwipopts.h 中开启 LWIP_DHCP 后，
        // 此处改为 dhcp_start(&g_netif) 并跳过静态 netif_add 参数
        sys_print("[lwIP] DHCP requested but LWIP_DHCP not enabled, fallback to static\r\n");
    }

    // 2. 将网卡挂载进 lwIP 路由表
    netif_add(&g_netif, &ipaddr, &netmask, &gw, nullptr, ethernetif_init, tcpip_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    sys_print("[lwIP] IPv4 interface configured (static)\r\n");

    // 3. 起飞网卡数据泵任务（Normal 优先级：轮询式驱动，每 5ms sleep 一次，无硬实时需求）
    uint32_t* rx_stack  = new uint32_t[512];
    uint32_t* app_stack = new uint32_t[512];
    if (!Scheduler::instance().create_task(
        reinterpret_cast<void (*)()>(ethernetif_input_task), rx_stack, 512 * sizeof(uint32_t),
        TaskPriority::Normal)) { // Normal：后台网卡轮询，sleep(5ms) 让出 CPU
        sys_print("[Kernel] ERROR: task table full, failed to spawn ethernetif_input_task!\r\n");
    }
    if (!Scheduler::instance().create_task(udp_echo_server_task, app_stack, 512 * sizeof(uint32_t),
        TaskPriority::Normal)) { // Normal：业务层 Echo 处理
        sys_print("[Kernel] ERROR: task table full, failed to spawn udp_echo_server_task!\r\n");
    }
}

extern "C" {
    extern uint32_t _heap_start;
    extern uint32_t _heap_end;
}

Mutex uart_mutex;

#include "uart_device.hpp"
#include "procfs.hpp"

// =========================================================================
// [核心系统进程] 低功耗空闲任务 (优先级最低，永远保持 Ready 状态)
// 当无任何业务任务可运行时，CPU 落入此处进入低功耗等待态
// =========================================================================
void sys_idle_task(void) {
    while (true) {
        Arch::wait_for_interrupt(); // 挂起 CPU 直到下一个中断到来，节省能耗
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

#include "mutex_pi.hpp"
MutexPI pi_lock;

void pi_test_low() {
    Scheduler::instance().sleep(500); // 错开系统启动的日志打印期
    sys_print("\r\n[Low] Task started, grabbing lock...\r\n");
    pi_lock.lock();
    sys_print("[Low] Lock acquired. Sleeping to let Mid & High wake up...\r\n");
    Scheduler::instance().sleep(500); 
    // 此时它被唤醒，如果 PI 成功，它的优先级已经被 High 拔高，它能抢占 Mid 运行！
    sys_print("[Low] Woken up with inherited priority. Releasing lock...\r\n");
    pi_lock.unlock();
    sys_print("[Low] Lock released. Base priority restored.\r\n");
    while (1) Scheduler::instance().sleep(10000);
}

void pi_test_mid() {
    Scheduler::instance().sleep(700); // 等 Low 先拿到锁
    sys_print("[Mid] Task woken up! Starting busy loop to starve Low...\r\n");
    // 疯狂循环模拟 CPU 占用，注意不能加 volatile 防止被优化没，而是加一点实际工作或者 volatile 计数
    for (volatile int i = 0; i < 50000000; i++) {}
    sys_print("[Mid] Busy loop finished. If PI worked, this prints AFTER High gets the lock.\r\n");
    while (1) Scheduler::instance().sleep(10000);
}

void pi_test_high() {
    Scheduler::instance().sleep(800); // 等 Mid 开始疯狂占用 CPU 后再醒来
    sys_print("[High] Task woken up! Trying to grab lock...\r\n");
    pi_lock.lock();
    sys_print("[High] Lock acquired! Priority Inheritance SUCCESS!\r\n");
    pi_lock.unlock();
    while (1) Scheduler::instance().sleep(10000);
}

#include "timer.hpp"
#include "posix.hpp"
#include "work_queue.hpp"

// 工作队列守护线程的入口包裹函数
void workqueue_daemon_entry(void) {
    WorkQueue::instance().worker_task();
}

// 定时器守护线程的入口包裹函数
void timer_daemon_entry(void) {
    TimerManager::instance().daemon_task();
}

// 用户回调：定时器到期时执行
void my_timer_callback(void* arg) {
    int fd = open("/dev/uart0", 0);
    if (fd >= 0) {
        write(fd, "\r\n[Timer Callback] Software Timer Triggered asynchronously!\r\n", 61);
        close(fd);
    }
}

void posix_app_task(void) {
    int fd = open("/dev/uart0", 0);
    if (fd >= 0) {
        // 创建一个 2000 毫秒（2秒）周期触发的软件定时器
        TimerManager::instance().start_timer(2000, TimerType::Periodic, my_timer_callback);
        write(fd, "\r\n[App] Software Timer (2s) scheduled.\r\n", 40);

        while (1) {
            write(fd, "[App] Main app loop running...\r\n", 32);
            Scheduler::instance().sleep(3000); // 故意睡 3 秒，和定时器的 2 秒产生异步交错
        }
        close(fd);
    }
    while (1) { Scheduler::instance().sleep(10000); } 
}

extern "C" void kernel_main(void) {
    uart_init();
    KernelHeap::instance().init(&_heap_start, &_heap_end);
    VfsManager::instance().init();

    RamFile* temp_file = new RamFile(1024);
    RamFile* elf_file = new RamFile(1024);

    // 挂载 设备 和 /tmp 目录下的虚拟文件
    DeviceRegistry::instance().register_device(new UartDevice("uart0"));
    VfsManager::instance().mount("/tmp/log.txt", (VNode*)temp_file);
    VfsManager::instance().mount("/tmp/app.elf", (VNode*)elf_file);
    
    // 挂载 ProcFS 虚拟节点
    VfsManager::instance().mount("/proc/meminfo", new MemInfoNode());
    VfsManager::instance().mount("/proc/taskinfo", new TaskInfoNode());
    
    // 初始化调度器
    Scheduler::instance().init();

    // ── 任务优先级分配表 ──────────────────────────────────────────
    // sys_idle_task   : Idle     — CPU 兜底进程，永不休眠
    // shell_task      : High     — 交互终端，响应键盘输入
    // lwIP net tasks  : Realtime — 网络 RX 数据泵（在 tcpip_init_done 中创建）
    // udp_echo_task   : Normal   — 业务层 Echo 处理
    // ─────────────────────────────────────────────────────────────
    uint32_t* idle_stack  = new uint32_t[128];
    uint32_t* shell_stack = new uint32_t[512];

    // 1. 空闲进程：优先级最低，负责 CPU 低功耗兜底
    if (!Scheduler::instance().create_task(sys_idle_task, idle_stack, 128 * sizeof(uint32_t),
        TaskPriority::Idle)) {
        sys_print("[Kernel] FATAL: failed to spawn sys_idle_task!\r\n");
    }

    // 2. 交互终端：高优先级响应用户键盘
    extern void shell_task(void);
    if (!Scheduler::instance().create_task(shell_task, shell_stack, 512 * sizeof(uint32_t),
        TaskPriority::High)) {
        sys_print("[Kernel] FATAL: failed to spawn shell_task!\r\n");
    }

    // 3. PI Mutex 测试任务
    Scheduler::instance().create_task(pi_test_low, new uint32_t[128], 128*sizeof(uint32_t), TaskPriority::Low);
    Scheduler::instance().create_task(pi_test_mid, new uint32_t[128], 128*sizeof(uint32_t), TaskPriority::Normal);
    Scheduler::instance().create_task(pi_test_high, new uint32_t[128], 128*sizeof(uint32_t), TaskPriority::High);

    // 4. 定时器守护进程与测试 App
    Scheduler::instance().create_task(timer_daemon_entry, new uint32_t[256], 256*sizeof(uint32_t), TaskPriority::Realtime);
    Scheduler::instance().create_task(posix_app_task, new uint32_t[256], 256*sizeof(uint32_t), TaskPriority::Low);

    // 5. 工作队列守护进程 (使用 High 优先级)
    Scheduler::instance().create_task(workqueue_daemon_entry, new uint32_t[256], 256*sizeof(uint32_t), TaskPriority::High);

    // 起 lwIP 协议栈引擎（内部回调中将以 Realtime 优先级注册网卡 RX 任务）
    sys_print("[lwIP] Initializing TCP/IP Engine...\r\n");
    tcpip_init(tcpip_init_done, nullptr);

    // 启动调度器：正确引导第一个任务（通过 PSP/bx 跳入，不破坏栈帧）
    // 调度器从此接管 CPU，永不返回
    Scheduler::instance().start();
}
