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
#include "task_notify.hpp"
#include "signal.hpp"
#include "frame_scheduler.hpp"
#include "../drivers/display/oled_driver.hpp"
#include "../drivers/display/framebuffer.hpp"
#include "../drivers/input/touch_driver.hpp" // 引入触控驱动
#include "../drivers/input/input_event.hpp"  // 引入输入协议
extern Mutex uart_mutex;
#include "mpu.hpp"

#ifdef CONFIG_NET_LWIP
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/api.h"
#include "arch_api.hpp"

extern err_t ethernetif_init(struct netif *netif);
extern void ethernetif_input_task(void);

struct netif g_netif;
#endif // CONFIG_NET_LWIP

#ifdef CONFIG_NET_LWIP
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
    const NetIpv4Config cfg = get_default_ipv4_config();
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
#endif // CONFIG_NET_LWIP

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

#include "mutex.hpp"
Mutex pi_lock;

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

#ifdef CONFIG_WORK_QUEUE
// 工作队列守护线程的入口包裹函数
void workqueue_daemon_entry(void) {
    WorkQueue::instance().worker_task();
}
#endif

#ifdef CONFIG_TIMER_MANAGER
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
#endif

// =========================================================================
// [核心系统进程] 任务通知与 POSIX 信号测试应用
// ==========================================
static uint32_t g_receiver_task_id = 0;

void receiver_task(void) {
    int fd = open("/dev/uart0", 0);

    // 1. 绑定 POSIX SIGUSR1 信号的异步回调
    signal(SIGUSR1, [](int sig) {
        int fd = open("/dev/uart0", 0);
        write(fd, "\r\n>>> [POSIX Signal Handler] SIGUSR1 intercepted asynchronously! <<<\r\n", 71);
        close(fd);
    });

    write(fd, "[Receiver] Ready and waiting for zero-overhead Task Notifications...\r\n", 70);
    close(fd);

    while (true) {
        // 2. 0 内存开销、0 耗时等待任务通知
        uint32_t val = TaskNotify::take(); 
        
        fd = open("/dev/uart0", 0);
        write(fd, "[Receiver] Task Notification Received! Value: 0x", 48);
        
        // 简单以十六进制打印输出
        char hex_str[11];
        for (int i = 7; i >= 0; i--) {
            int nibble = (val >> (i * 4)) & 0xF;
            hex_str[7 - i] = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
        }
        hex_str[8] = '\r';
        hex_str[9] = '\n';
        hex_str[10] = '\0';
        write(fd, hex_str, 10);
        close(fd);
    }
}

void sender_task(void) {
    Scheduler::instance().sleep(1500); // 等待接收线程就绪

    while (true) {
        // 测试 1：发送 FreeRTOS 任务通知
        TaskNotify::give(g_receiver_task_id, 0xA5A5);
        Scheduler::instance().sleep(2000);

        // 测试 2：跨线程发送 POSIX 异步软件信号
        kill(g_receiver_task_id, SIGUSR1);
        Scheduler::instance().sleep(2000);
    }
}

// 1. 实例化全局 128x128 智能手表 OLED 驱动与显存缓冲
OledDriver g_oled("oled0", 128, 128);
FrameBuffer<128, 128> g_fb;

// 2. 实例化全局 I2C 触控屏驱动，命名为 touch0
TouchDriver g_touch("touch0", 128, 128);

// 可拖拽的 UI 控件组件 (比如一个 24x24 的手表智能应用卡片)
struct DraggableWidget {
    uint16_t    x, y;
    uint16_t    width, height;
    ColorRGB565 color;
    bool        is_dragging;
};

// ==========================================
// 手表主 UI 界面与拖拽交互引擎 (受 30FPS 窗口调度)
// ==========================================
void ui_render_task(void) {
    g_oled.open();
    
    // 打开触控屏驱动，获得 POSIX 文件描述符！
    int touch_fd = open("/dev/touch0", 0);
    int console_fd = open("/dev/uart0", 0);
    
    write(console_fd, "\r\n[BlueOS GUI] Input Event Subsystem & Drag Engine Mounted.\r\n", 61);
    close(console_fd);

    // 初始化控件初始坐标于屏幕左上方 (30, 30)
    DraggableWidget widget = { 30, 30, 20, 20, 0x07E0, false }; // 亮绿色卡片

    // 第一帧：绘制背景与初始控件
    g_fb.clear(0x0000);
    g_fb.fill_rect(widget.x - widget.width/2, widget.y - widget.height/2, widget.width, widget.height, widget.color);
    g_fb.flush(g_oled);
    
    FrameScheduler::instance().wait_for_next_frame();

    while (true) {
        // 1. 从 /dev/touch0 读取最新标准输入事件包
        TouchPoint touch;
        int bytes = read(touch_fd, reinterpret_cast<char*>(&touch), sizeof(TouchPoint));

        if (bytes == sizeof(TouchPoint) && touch.is_valid) {
            console_fd = open("/dev/uart0", 0);
            
            // 2. 判断输入交互并计算 UI 拖拽响应
            if (touch.state == TouchState::PRESSED || touch.state == TouchState::MOVING) {
                if (!widget.is_dragging) {
                    write(console_fd, "\r\n👇 [Input Event] Touch PRESSED! Widget Dragging Started...\r\n", 62);
                    widget.is_dragging = true;
                    widget.color = 0xF800; // 拖拽时高亮变红！
                }

                // 【关键步骤：脏区域合围】
                // a. 先在控件旧坐标填补纯黑背景（擦除旧残影，自动标记旧区域为脏）
                g_fb.fill_rect(widget.x - widget.width/2, widget.y - widget.height/2, widget.width, widget.height, 0x0000);

                // b. 更新控件坐标到手指触摸处
                widget.x = touch.x;
                widget.y = touch.y;

                // c. 在新坐标绘制控件（自动标记新区域为脏）
                g_fb.fill_rect(widget.x - widget.width/2, widget.y - widget.height/2, widget.width, widget.height, widget.color);

                write(console_fd, "    🔄 [UI Drag] Widget moving to X...\r\n", 40);
            } else if (touch.state == TouchState::RELEASED && widget.is_dragging) {
                write(console_fd, "\r\n👆 [Input Event] Touch RELEASED! Widget Dragging Dropped.\r\n", 61);
                widget.is_dragging = false;
                widget.color = 0x07E0; // 抬起手指，颜色恢复常态亮绿
                
                g_fb.fill_rect(widget.x - widget.width/2, widget.y - widget.height/2, widget.width, widget.height, widget.color);
            }
            close(console_fd);

            // 3. 将本帧动态求合集的微小脏包围盒通过 SPI 送给 OLED 屏！
            g_fb.flush(g_oled);
        }

        // 4. 遵守 33ms V-Sync 帧规律，释放 CPU 给后台算法
        FrameScheduler::instance().wait_for_next_frame();
    }
}

// ==========================================
// 2. 后台健康传感器数据处理 (NORMAL 帧间执行)
// ==========================================
void sensor_log_task(void) {
    while (true) {
        int fd = open("/dev/uart0", 0);
        write(fd, "        ⚙️ [Inter-Frame] Background Sensor Log Running in 21ms gap!\r\n", 71);
        close(fd);

        // 模拟较长的传感器卡尔曼滤波数学运算
        for (volatile int i = 0; i < 400000; i++);
        
        Scheduler::instance().sleep(10); // 稍微出让一下，让打印更工整
    }
}

// =========================================================================
// [核心系统进程] 黑客应用任务
// ==========================================
void hacker_app_task(void) {
    sys_print("\r\n[Hacker App] Attempting to crack kernel security...\r\n");

    // 尝试一：通过系统调用合法打印（正常通过）
    sys_print("[Hacker App] Step 1: Legal syscall works fine.\r\n");

    Scheduler::instance().sleep(3000); // 延时3秒，确保前面系统的启动日志能完整打印出来

    // 此时主动将自身的 CPU 特权级降级为 Unprivileged (普通应用态)
    sys_print("[Hacker App] Dropping CPU privilege level to User Mode...\r\n");
    __asm__ volatile (
        "mrs r0, control \n\t"
        "orr r0, r0, #1 \n\t"    // Set Bit 0 (nPRIV) to 1 -> Unprivileged
        "msr control, r0 \n\t"
        "isb \n\t" 
        : : : "r0", "memory"
    );

    // 尝试二：恶意构造一个指向内核核心变量的指针，试图修改系统的 Tick！
    sys_print("[Hacker App] Step 2: Attempting illegal write to kernel tick_count...\r\n");
    
    extern volatile uint32_t tick_count;
    tick_count = 0xDEADBEEF; // 这一行一旦执行，触发 MPU MemManage！

    // 永远不会执行到这一步！
    sys_print("[Hacker App] Oh no! System hacked!\r\n"); 
}

// 分配给 hacker_app_task 的栈，大小必须是 2 的幂次方且地址对齐
alignas(1024) uint8_t hacker_stack[1024];

extern "C" void kernel_main(void) {
    uart_init();
    KernelHeap::instance().init(&_heap_start, &_heap_end);

    // ==========================================
    // 激活 MPU 空间隔离安全防火墙
    // ==========================================
    MPU::instance().disable();

    // 1. 保护 Flash 代码区 (假设从 0x00000000 开始，大小 256KB = 2^18)
    // 权限：全系统只读 (AP_ALL_RO)，允许执行代码
    MPU::instance().configure_region(0, 0x00000000, 18, MPU::AP_ALL_RO, false);

    // 2. 锁死全局 RAM 内存空间 (假设从 0x20000000 开始，大小 64KB = 2^16)
    // 权限：仅内核特权态读写 (AP_PRIV_RW)，严禁用户态触碰！
    MPU::instance().configure_region(1, 0x20000000, 16, MPU::AP_PRIV_RW, true);

    MPU::instance().enable();
    sys_print("[Security] MPU Memory Protection Unit Activated.\r\n");

    VfsManager::instance().init();

#ifdef CONFIG_FS_RAMFS
    RamFile* temp_file = new RamFile(1024);
    RamFile* elf_file = new RamFile(1024);
    VfsManager::instance().mount("/tmp/log.txt", (VNode*)temp_file);
    VfsManager::instance().mount("/tmp/app.elf", (VNode*)elf_file);
#endif

#ifdef CONFIG_DEVICE_UART
    // 挂载 设备 和 /tmp 目录下的虚拟文件
    DeviceRegistry::instance().register_device(new UartDevice("uart0"));
#endif
    DeviceRegistry::instance().register_device(&g_oled);
    // 【内核注册】将 I2C 触摸屏挂载到 /dev/touch0
    DeviceRegistry::instance().register_device(&g_touch);
    
#ifdef CONFIG_FS_PROCFS
    // 挂载 ProcFS 虚拟节点
    VfsManager::instance().mount("/proc/meminfo", new MemInfoNode());
    VfsManager::instance().mount("/proc/taskinfo", new TaskInfoNode());
#endif
    
    // 初始化调度器
    Scheduler::instance().init();

    // ── 任务优先级分配表 ──────────────────────────────────────────
    // sys_idle_task   : Idle     — CPU 兜底进程，永不休眠
    // shell_task      : High     — 交互终端，响应键盘输入
    // lwIP net tasks  : Realtime — 网络 RX 数据泵（在 tcpip_init_done 中创建）
    // udp_echo_task   : Normal   — 业务层 Echo 处理
    // ─────────────────────────────────────────────────────────────
    constexpr uint32_t STACK_SIZE_IDLE = 128;
    constexpr uint32_t STACK_SIZE_SHELL = 512;
    constexpr uint32_t STACK_SIZE_TEST = 128;
    constexpr uint32_t STACK_SIZE_DAEMON = 256;

    uint32_t* idle_stack  = new uint32_t[STACK_SIZE_IDLE];
    uint32_t* shell_stack = new uint32_t[STACK_SIZE_SHELL];

    // 1. 空闲进程：优先级最低，负责 CPU 低功耗兜底
    if (!Scheduler::instance().create_task(sys_idle_task, idle_stack, STACK_SIZE_IDLE * sizeof(uint32_t),
        TaskPriority::Idle)) {
        sys_print("[Kernel] FATAL: failed to spawn sys_idle_task!\r\n");
    }

    // 2. 交互终端：高优先级响应用户键盘
    extern void shell_task(void);
    if (!Scheduler::instance().create_task(shell_task, shell_stack, STACK_SIZE_SHELL * sizeof(uint32_t),
        TaskPriority::High)) {
        sys_print("[Kernel] FATAL: failed to spawn shell_task!\r\n");
    }

    // 3. PI Mutex 测试任务
    Scheduler::instance().create_task(pi_test_low, new uint32_t[STACK_SIZE_TEST], STACK_SIZE_TEST*sizeof(uint32_t), TaskPriority::Low);
    Scheduler::instance().create_task(pi_test_mid, new uint32_t[STACK_SIZE_TEST], STACK_SIZE_TEST*sizeof(uint32_t), TaskPriority::Normal);
    Scheduler::instance().create_task(pi_test_high, new uint32_t[STACK_SIZE_TEST], STACK_SIZE_TEST*sizeof(uint32_t), TaskPriority::High);

    // 4. Hacker App Task (带有 MPU 沙盒隔离保护的测试线程)
    Scheduler::instance().create_task(hacker_app_task, reinterpret_cast<uint32_t*>(hacker_stack), sizeof(hacker_stack), TaskPriority::Low, 10);

    // 5. Task Notify & POSIX Signal Test Tasks
    TaskControlBlock* rx_tcb = Scheduler::instance().create_task(receiver_task, new uint32_t[STACK_SIZE_TEST], STACK_SIZE_TEST*sizeof(uint32_t), TaskPriority::Normal);
    if (rx_tcb) g_receiver_task_id = rx_tcb->id;
    Scheduler::instance().create_task(sender_task, new uint32_t[STACK_SIZE_TEST], STACK_SIZE_TEST*sizeof(uint32_t), TaskPriority::Normal);

    // 6. 蓝河 Frame-Aware Scheduler 任务注册
    uint32_t* ui_stack = new uint32_t[STACK_SIZE_TEST];
    uint32_t ui_tid = FrameScheduler::instance().create_frame_task(ui_render_task, ui_stack, STACK_SIZE_TEST * sizeof(uint32_t), FramePriority::CRITICAL);

    uint32_t* sensor_stack = new uint32_t[STACK_SIZE_TEST];
    FrameScheduler::instance().create_frame_task(sensor_log_task, sensor_stack, STACK_SIZE_TEST * sizeof(uint32_t), FramePriority::NORMAL);

    // 【蓝河引擎绑定】初始化 30FPS 调度器，并绑定 UI 主任务的 ID
    FrameScheduler::instance().init(30, ui_tid);

#ifdef CONFIG_TIMER_MANAGER
    // 4. 定时器守护进程与测试 App
    Scheduler::instance().create_task(timer_daemon_entry, new uint32_t[STACK_SIZE_DAEMON], STACK_SIZE_DAEMON*sizeof(uint32_t), TaskPriority::Realtime);
    Scheduler::instance().create_task(posix_app_task, new uint32_t[STACK_SIZE_DAEMON], STACK_SIZE_DAEMON*sizeof(uint32_t), TaskPriority::Low);
#endif

#ifdef CONFIG_WORK_QUEUE
    // 5. 工作队列守护进程 (使用 High 优先级)
    Scheduler::instance().create_task(workqueue_daemon_entry, new uint32_t[STACK_SIZE_DAEMON], STACK_SIZE_DAEMON*sizeof(uint32_t), TaskPriority::High);
#endif

#ifdef CONFIG_NET_LWIP
    // 起 lwIP 协议栈引擎（内部回调中将以 Realtime 优先级注册网卡 RX 任务）
    sys_print("[lwIP] Initializing TCP/IP Engine...\r\n");
    tcpip_init(tcpip_init_done, nullptr);
#endif

    // 启动调度器：正确引导第一个任务（通过 PSP/bx 跳入，不破坏栈帧）
    // 调度器从此接管 CPU，永不返回
    Scheduler::instance().start();
}
