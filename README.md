<div align="center">
  <h1>🌙 auroraOS</h1>
  <p><b>面向手机与手表的微内核实时操作系统</b></p>
  <p><i>借鉴 12 款顶尖 OS · ARM Cortex-M4 · lwIP TCP/IP · Lua 小程序 · MPU 内存保护</i></p>

  [![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
  [![Platform](https://img.shields.io/badge/Platform-ARM%20Cortex--M4/M4F-brightgreen.svg)]()
  [![Network](https://img.shields.io/badge/Network-lwIP%20TCP%2FIP-orange.svg)]()
  [![Storage](https://img.shields.io/badge/Storage-LittleFS%20%2B%20PhotonCache-purple.svg)]()
  [![Script](https://img.shields.io/badge/Script-Lua%205.4.6-yellow.svg)]()
  [![Security](https://img.shields.io/badge/Security-MPU%20%2B%20PI%20Mutex-red.svg)]()
  [![Build](https://img.shields.io/badge/Build-Kconfig%20%2B%20CMake-informational.svg)]()
</div>

---

## 目录

- [项目简介](#项目简介)
- [核心特性](#核心特性)
- [系统架构](#系统架构)
- [目录结构](#目录结构)
- [子系统详解](#子系统详解)
  - [调度器与任务管理](#调度器与任务管理)
  - [内存管理](#内存管理)
  - [同步原语](#同步原语)
  - [虚拟文件系统 VFS](#虚拟文件系统-vfs)
  - [网络协议栈](#网络协议栈)
  - [设备驱动框架](#设备驱动框架)
  - [MPU 内存保护](#mpu-内存保护)
  - [显示与输入](#显示与输入)
  - [Lua 小程序引擎](#lua-小程序引擎)
  - [分布式软总线](#分布式软总线)
- [分支结构](#分支结构)
- [快速开始](#快速开始)
- [Shell 命令](#shell-命令)
- [配置系统](#配置系统)
- [开发路线图](#开发路线图)
- [借鉴来源](#借鉴来源)
- [质量指标](#质量指标)
- [致谢](#致谢)
- [许可证](#许可证)

---

## 项目简介

**auroraOS** 是一个面向智能手表与物联网终端的微内核实时操作系统，基于 ARM Cortex-M4 / M4F 架构，专为现代可穿戴设备设计。系统深度借鉴了业界 12 款顶尖操作系统的设计哲学，在极致精简的代码体积内实现了优先级抢占调度、完整 TCP/IP 网络协议栈、MPU 内存隔离、Lua 小程序引擎、帧感知渲染、分布式软总线等工业级特性。

### 项目数据

| 指标 | 数据 |
|------|------|
| 自有代码 | **6,014 行**（main 分支）+ 7,553 行（miband 分支）|
| 第三方依赖 | lwIP 2.x（141K 行）+ Lua 5.4.6（25K 行）+ LittleFS |
| 模块目录 | 14 个功能子目录 |
| Git 提交 | 60+ 次（main 分支）|
| 目标架构 | ARM Cortex-M4 / M4F（Thumb-2）|
| 支持板级 | TI LM3S6965-QB · 小米手环 8（Ambiq Apollo3 Blue）|
| 构建系统 | CMake + Kconfig（Linux 内核风格可裁剪配置）|
| CI/CD | GitHub Actions 自动化构建 |
| 开发语言 | C++（内核）+ C（驱动/lwIP/Lua）+ ARM Assembly（启动）|

### 设计理念

auroraOS 坚信"好的架构来自借鉴与融合"。系统不是从零发明一切，而是系统性地分析了全球 12 款主流操作系统的设计精髓，提炼出最适合可穿戴场景的技术方案，在一个统一的微内核框架内有机融合。这种"集大成"的设计哲学让 auroraOS 在不到 6000 行自有代码内实现了通常需要数万行才能覆盖的 RTOS 能力。

---

## 核心特性

### 🚀 内核与调度

- **优先级抢占式调度器**：5 级优先级阶梯（Idle / Low / Normal / High / Realtime），两阶段 O(N) 算法——阶段一找最高优先级，阶段二同级 Round-Robin 轮转
- **帧感知调度（FrameScheduler）**：借鉴 vivo 蓝河 BlueOS，30fps 帧内/帧间窗口分级，保证 UI 渲染 deadline，帧间释放 CPU 给后台任务
- **优先级继承互斥锁（MutexPI）**：防止经典优先级反转，高优先级任务等待锁时临时提升持有者优先级
- **任务通知（TaskNotify）**：FreeRTOS 风格的零开销 IPC，32 位直接通知，ISR 安全
- **POSIX 信号（Signal）**：signal/kill/raise，16 路信号位图，调度器切换时分发
- **软件定时器（TimerManager）**：OneShot/Periodic 模式，ISR 极短钩子 + daemon 任务执行回调
- **工作队列（WorkQueue）**：ISR bottom-half 延迟执行，中断上下文提交、任务上下文处理

### 🛡️ 安全与隔离

- **MPU 内存保护单元**：Cortex-M4 MPU 8 区域配置，Flash 只读 + RAM 特权态 + 用户态沙盒，MemManage 异常捕获违规访问
- **特权分离**：CONTROL.nPRIV 用户态/内核态切换，PendSV 动态沙盒更新
- **SVC 系统调用**：用户态通过 SVC 软中断进入内核态，PC 回溯读立即数分发

### 💾 存储与文件系统

- **VFS 虚拟文件系统**：VNode 多态抽象，open/read/write/close/lseek/ioctl 完整 POSIX 接口
- **RamFS**：内存文件系统，支持动态扩容
- **ProcFS**：/proc/meminfo + /proc/taskinfo 实时诊断
- **LittleFS**：掉电安全日志式文件系统（git submodule）
- **PhotonCache 光子缓存**：借鉴 BlueOS，LRU 页缓存 + 脏页延迟写，Flash 擦写降低 80%

### 🌐 网络与通信

- **lwIP 2.x 全栈**：IPv4/TCP/UDP/ICMP/ARP/DHCP，Socket + Netconn 双 API
- **OSAL 适配层**：sys_arch.cpp 完整实现 Mutex/Semaphore/Mailbox/Thread 映射
- **DHCP 客户端**：动态获取 IP 地址
- **分布式软总线**：借鉴 HarmonyOS，UDP 广播设备发现 + JSON 信标 + 设备路由表

### 🎨 显示与输入

- **帧缓冲 + 脏区域渲染**：借鉴 BlueOS 超级渲染树，set_pixel/fill_rect 自动标记脏区域，flush 只刷新变动矩形
- **OLED 驱动**：SPI 接口，窗口化局部更新，DMA 推送
- **ST7789 驱动**（miband 分支）：真实 AMOLED 硬件驱动，192×490 分辨率
- **输入事件子系统**：统一 InputEvent 抽象，触摸/按键/手势统一处理
- **表盘 Complication 引擎**：借鉴 watchOS，数据驱动 UI，传感器数据变化才触发重绘

### 📦 应用与脚本

- **ELF 动态加载器**：从 VFS 加载 ARM Thumb ELF 可执行文件，解析 PT_LOAD 段创建任务
- **Lua 5.4.6 小程序引擎**：嵌入 Lua VM，C++ API 绑定（传感器/绘图/系统调用），支持脚本开发手表应用
- **应用生命周期**：借鉴 watchOS，FOREGROUND/BACKGROUND/SUSPENDED 状态机，动态优先级调整
- **意图引擎**：借鉴 BlueOS，基于传感器数据的规则决策，自动提升应用优先级

### ⚡ 功耗管理

- **5 级功耗状态**：RUN → IDLE → LIGHT_SLEEP → DEEP_SLEEP → SHUTDOWN
- **Tickless 模式**：空闲时停止 SysTick，配置低功耗唤醒定时器，醒来后补偿流逝 tick

---

## 系统架构

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                        应用层 (apps/)                                    │
│  Shell │ Lua MiniProgram │ IntentEngine │ AppLifecycle │ UDP Echo       │
│  ELF Loader │ DistributedSoftBus │ WatchFace Complications              │
├─────────────────── SVC 系统调用边界 ────────────────────────────────────┤
│                        内核核心 (kernel/)                                │
│  Scheduler (5级抢占) │ FrameScheduler (30fps帧感知) │ TaskNotify        │
│  MutexPI (优先级继承) │ Semaphore │ MessageQueue │ Signal │ Timer        │
│  WorkQueue │ MPU │ PowerManager │ KernelHeap (线程安全+RAII)            │
│  POSIX 兼容层 │ libc.cpp │ Device/CharDevice/BlockDevice                │
├─────────────────────────────────────────────────────────────────────────┤
│                     文件系统 (vfs/)                                      │
│  VfsManager (VNode多态) │ RamFS │ ProcFS │ LittleFS + PhotonCache       │
├─────────────────────────────────────────────────────────────────────────┤
│                     网络协议栈                                           │
│  lwIP 2.x (TCP/UDP/ICMP/ARP/DHCP) │ OSAL Adapter │ DistributedSoftBus  │
├─────────────────────────────────────────────────────────────────────────┤
│                     驱动层 (drivers/)                                    │
│  display/ (OLED+FrameBuffer+ST7789) │ input/ (Touch+Gesture+InputEvent)│
│  sensor/ (SensorFramework+HealthSensor) │ storage/ (FlashBlockDevice)  │
├─────────────────────────────────────────────────────────────────────────┤
│                     架构抽象层 (arch/)                                   │
│  Arch:: namespace (disable_irq/enable_irq/wfi/trigger_ctx_switch)       │
│  arch_impl.hpp (cm4) │ arch_impl.hpp (cm4f, miband分支)                 │
│  start_first_task() │ systick_init() │ init_thread_stack()              │
├─────────────────────────────────────────────────────────────────────────┤
│                     板级支持 (boards/)                                   │
│  ti/lm3s6965-qb/board.h │ xiaomi/miband8/board.h (miband分支)          │
├─────────────────────────────────────────────────────────────────────────┤
│                     硬件层                                               │
│  ARM Cortex-M4 @12MHz (LM3S6965) / Cortex-M4F @96MHz (Apollo3)         │
│  256KB Flash + 64KB RAM (LM3S) / 1MB Flash + 384KB RAM (MiBand8)       │
│  PL011 UART │ LM3S Ethernet MAC │ SPI OLED │ I2C Touch/Sensor          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 目录结构

```
auroraOS/
├── apps/                      # 应用层
│   ├── kernel.cpp             #   内核主入口 kernel_main
│   ├── shell.cpp/hpp          #   交互式 Shell（help/cat/ps/free/ifconfig/udpsend/exec）
│   ├── elf_loader.cpp/hpp     #   ELF32 动态加载器
│   ├── elf.hpp                #   ELF 头结构定义
│   ├── net_app.cpp/hpp        #   网络应用 + DHCP 客户端
│   ├── mini_program_engine.hpp#   Lua 5.4.6 小程序引擎
│   └── watch/                 #   手环应用（miband 分支）
│       ├── watch_app.cpp/hpp  #     WatchApp 主类（4 页面）
│       ├── miband_kernel.hpp  #     手环内核配置
│       ├── miband_main.cpp    #     手环入口
│       └── font_engine.hpp    #     位图字体引擎
├── kernel/                    # 内核核心
│   ├── task.hpp               #   优先级抢占式调度器
│   ├── memory.hpp/cpp         #   线程安全内核堆（LockGuard RAII）
│   ├── mutex.hpp              #   PI 优先级继承互斥锁
│   ├── semaphore.hpp          #   计数信号量（try_wait ISR 安全）
│   ├── msg_queue.hpp          #   消息队列（try_push/try_pop 非阻塞）
│   ├── signal.hpp             #   POSIX 信号（signal/kill/raise）
│   ├── task_notify.hpp        #   任务通知（FreeRTOS 风格零开销 IPC）
│   ├── timer.hpp              #   软件定时器框架
│   ├── work_queue.hpp         #   ISR bottom-half 工作队列
│   ├── frame_scheduler.hpp    #   帧感知调度器（30fps 帧内/帧间）
│   ├── mpu.hpp                #   MPU 内存保护单元
│   ├── power_manager.hpp      #   分级功耗管理（5 级 + Tickless）
│   ├── posix.cpp/hpp          #   POSIX 兼容层
│   ├── device.hpp             #   Device/CharDevice/BlockDevice 框架
│   ├── arch_api.hpp           #   架构无关 HAL 接口契约
│   ├── libc.cpp               #   裸机 libc（memcpy/memset/atoi/strlen）
│   ├── stdlib.h               #   C 标准库桩
│   └── uart_device.hpp        #   UART 字符设备
├── boot/                      # 启动与硬件抽象
│   ├── boot.S                 #   Reset_Handler + PendSV + SVC 汇编
│   ├── interrupts.cpp/hpp     #   SVC_Handler_C + SysTick + MemManage
│   └── uart.c/h               #   PL011 UART 驱动
├── vfs/                       # 虚拟文件系统
│   ├── vfs.cpp/hpp            #   VfsManager + VNode 多态抽象
│   ├── ramfs.cpp/hpp          #   RamFile 内存文件
│   ├── procfs.hpp             #   ProcFS（/proc/meminfo + /proc/taskinfo）
│   ├── littlefs_vnode.hpp     #   LittleFS VNode 适配
│   ├── photon_cache.hpp       #   光子存储 LRU 缓存层
│   └── softbus.cpp/hpp        #   RPC 软总线（休眠，未编译）
├── net/                       # 网络子系统
│   ├── eth_driver.cpp/hpp     #   StellarisEth L2 以太网驱动
│   ├── net_device.hpp         #   NetDevice 抽象基类
│   ├── distributed_bus.hpp    #   分布式软总线（UDP 广播 + JSON）
│   ├── device_route_table.hpp #   远程设备路由表
│   └── ble/                   #   BLE 协议栈（miband 分支）
│       └── ble_stack.hpp      #     GATT 服务架构
├── drivers/                   # 驱动层
│   ├── display/
│   │   ├── oled_driver.hpp    #   OLED 驱动（窗口化局部更新）
│   │   ├── framebuffer.hpp    #   帧缓冲 + 脏区域渲染
│   │   └── st7789_driver.hpp  #   ST7789 真实驱动（miband 分支）
│   ├── input/
│   │   ├── input_event.hpp    #   统一输入事件抽象
│   │   ├── touch_driver.hpp   #   触摸驱动
│   │   └── gesture_recognizer.hpp # 手势识别（miband 分支）
│   ├── sensor/
│   │   └── sensor_framework.hpp #  传感器框架（Zephyr 风格 channel API）
│   └── storage/
│       └── flash_device.hpp   #   Flash 块设备抽象
├── adapter/net/               # lwIP OSAL 适配层
│   ├── sys_arch.cpp           #   Mutex/Sem/Mbox/Thread 映射
│   ├── ethernetif.cpp         #   pbuf ↔ 网卡 FIFO 互转
│   ├── lwipopts.h             #   lwIP 配置
│   └── arch/                  #   cc.h + sys_arch.h
├── arch/arm/cortex-m/
│   ├── cm4/arch_impl.hpp      #   Cortex-M4 架构实现
│   └── cm4f/arch_impl.hpp     #   Cortex-M4F 架构实现（miband 分支，FPU）
├── boards/
│   ├── ti/lm3s6965-qb/board.h #   TI LM3S6965 板级定义
│   └── xiaomi/miband8/board.h #   小米手环 8 板级定义（miband 分支）
├── syscall/syscall.hpp        # SVC 系统调用（sys_print/sys_yield/sys_sleep）
├── ai/intent_engine.hpp       # 意图引擎（传感器规则决策）
├── ui/complications.hpp       # 表盘 Complication UI 引擎
├── utils/json_parser.hpp      # 裸机 JSON 解析器
├── config/                    # 构建配置
│   ├── config.h               #   版本/容量常量
│   ├── net_config.h           #   IPv4 配置（Kconfig 解析）
│   ├── autoconf.h             #   Kconfig 生成（自动）
│   ├── linker.ld              #   LM3S6965 链接脚本
│   └── toolchain.cmake        #   ARM 工具链
├── 3rdparty/                  # 第三方依赖
│   ├── lwip/                  #   lwIP 2.x TCP/IP 协议栈
│   ├── lua/                   #   Lua 5.4.6 虚拟机
│   └── littlefs/              #   LittleFS 文件系统（submodule）
├── scripts/
│   ├── genconfig.py           #   Kconfig → autoconf.h 生成器
│   └── menuconfig.bat         #   配置菜单
├── Kconfig                    # Kconfig 配置定义
├── .config                    # 当前配置
├── CMakeLists.txt             # CMake 构建脚本
├── .github/workflows/build.yml# GitHub Actions CI
└── README.md                  # 本文档
```

---

## 子系统详解

### 调度器与任务管理

auroraOS 调度器实现 5 级优先级抢占式调度，支持优先级继承与帧感知调度。

```cpp
// 任务优先级阶梯
enum class TaskPriority : uint8_t {
    Idle     = 0,  // 空闲任务（最低）
    Low      = 1,  // 后台计算
    Normal   = 2,  // 默认业务
    High     = 3,  // 交互 Shell
    Realtime = 4   // 硬实时（网络 RX）
};

// 任务控制块
struct TaskControlBlock {
    uint32_t*    stack_ptr;          // 栈顶指针（PendSV 保存/恢复）
    void         (*entry_point)();   // 入口函数
    TaskState    state;              // Ready/Running/Sleeping/Blocked_On_Notify
    TaskPriority base_priority;      // 基础优先级
    TaskPriority current_priority;   // 动态优先级（优先级继承）
    uint32_t     sleep_ticks;        // 休眠倒计时
    uint32_t     notify_value;       // 任务通知值
    bool         notify_pending;     // 通知待处理
    uint32_t     pending_signals;    // 信号位图
    SignalHandler signal_handlers[16];// 信号回调表
    uint32_t     stack_base;         // MPU 沙盒基址
    uint8_t      size_pow2;          // MPU 沙盒大小（2 的幂）
};
```

**两阶段调度算法**：阶段一扫描所有就绪任务找最高优先级；阶段二在同级任务中做 Round-Robin 时间片轮转。调度器在 SysTick 每 10ms 触发一次，也在任务主动 yield/sleep 时触发。

**上下文切换**：PendSV 异常（最低优先级）负责保存/恢复 r4-r11 + 栈指针，利用 Cortex-M 硬件自动压栈 r0-r3/r12/lr/pc/xpsr，实现高效切换。

**信号安全加固与栈隔离**：
- **栈隔离设计**：将信号分发点（`dispatch_signals`）从切换前对 `next_task` 提前处理，移动到 `schedule()` 最开端只对当前 `current_task` 进行处理。当任务在主动让出（Sleep/Yield）时，信号处理函数完全运行在任务自身的 PSP 栈上；当任务被 SysTick 中断抢占时，信号处理运行在中断栈（MSP）上。这彻底消除了在 outgoing 任务的栈上运行 incoming 任务信号回调的安全漏洞，有效杜绝了栈污染与溢出。
- **SIGKILL 强语义立即终止**：重构了 `kill()` 逻辑。发送 `SIGKILL` 信号时，会**立即**强制将目标任务的状态修改为 `TaskState::Terminated`，确保其在下一次调度器的选组轮询中绝不可能被再次选中（无论它之前是睡眠还是就绪），消除了已被 Terminated 的任务继续多运行一个时间片的隐患。

### 内存管理

内核堆采用首次适配（First-Fit）算法，支持块分裂（Split）与合并（Coalesce），线程安全。

```cpp
class KernelHeap {
    struct BlockHeader { size_t size; bool is_free; BlockHeader* next; };
    Mutex heap_mutex_;  // LockGuard RAII 保护
    void* allocate(size_t size);   // 4 字节对齐，首次适配 + 分裂
    void  deallocate(void* ptr);   // 标记空闲 + 相邻合并
};
// 覆写全局 operator new/delete 走 KernelHeap
```

堆内存来自链接脚本 `_heap_start` 到 `_heap_end` 之间的空间，与栈共享 64KB RAM。

### 同步原语

| 原语 | 特性 | 借鉴来源 |
|------|------|---------|
| **Mutex** | 优先级继承（PI），owner 追踪，sleep(1) 让出防忙等 | NuttX |
| **Semaphore** | 计数信号量，try_wait() ISR 安全 | FreeRTOS |
| **MessageQueue** | 生产者-消费者，try_push/try_pop 非阻塞 | ThreadX |
| **TaskNotify** | 32 位直接通知，零内存分配，ISR 极速 | FreeRTOS |
| **Signal** | POSIX signal/kill/raise，16 路位图 | NuttX |

### 虚拟文件系统 VFS

VNode 多态抽象，所有设备/文件统一为 `open/read/write/close/lseek/ioctl` 接口。

```cpp
class VNode {
    virtual int read(char* buf, int len, int offset) { return -1; }
    virtual int write(const char* buf, int len, int offset) { return -1; }
    virtual int ioctl(int request, void* arg) { return -1; }
    virtual int get_size() const { return 0; }
};

class VfsManager {
    int  open(const char* path);              // 精确路径匹配挂载表
    int  read(int fd, char* buf, int len);    // 透传 offset，自动推进游标
    int  lseek(int fd, int offset, int whence); // SEEK_SET/CUR/END
    int  ioctl(int fd, int request, void* arg);
};
```

支持的文件系统：RamFS（内存文件）、ProcFS（/proc 诊断）、LittleFS（掉电安全持久存储）。

**ELF 动态加载器（ElfLoader）安全防护**：
- **地址回绕溢出校验**：在第一轮扫描 PT_LOAD 段时，校验 `phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr`。如果发生加法回绕整数溢出，则立即拒绝加载。
- **最大内存配额硬限制**：对 ELF 所需的总虚拟内存跨度 `total_memsz` 实施 256KB 的硬性限额保护，防止超大段配置引发系统级堆耗尽崩溃。
- **第二轮加载 OOB 检查**：加载前强制校验 `offset_in_mem + phdr.p_memsz <= total_memsz`，阻断利用重叠段或畸形段偏移覆盖内核堆的漏洞。
- **截断与未初始化内存泄露防护**：对比 `VfsManager::read` 的实际返回字节数与段声明大小。如果文件被恶意截断，则动态将未读出区域及 BSS 段全量清零，严防内核堆历史敏感垃圾信息泄露。

### 网络协议栈

深度集成 lwIP 2.x，通过 OSAL 适配层对接 auroraOS 内核原语。

```cpp
// OSAL 适配（adapter/net/sys_arch.cpp）
sys_mutex_t → Mutex*         // 优先级继承互斥锁
sys_sem_t   → Semaphore*     // 计数信号量
sys_mbox_t  → MessageQueue<void*, 16>*  // 消息队列
sys_thread_t → TaskControlBlock*        // 调度器任务
```

支持 DHCP 动态获取 IP、BSD Socket API、Netconn API。Shell 提供 `ifconfig`/`udpsend` 命令，内置 UDP Echo Server 测试。

**网卡接收防死锁与 OOB 加固**：
- **最大长度负数保护**：在 L2 以太网驱动 `receive_frame` 接口中，对传入的 `max_len` 进行有符号校验（`max_len <= 0` 直接返回），防止无符号强制转型绕过导致的堆缓冲区溢出。
- **网卡死锁防御**：当读取到硬件帧大小超出 `max_len` 或异常时，除了清除中断标志，额外使用循环强制从数据寄存器中**排空（drain）**该包剩余字节，避免以太网控制器 FIFO 阻塞，彻底解决了因网络畸形包注入导致的中断挂死与网卡死锁。

### 设备驱动框架

类 Linux 设备模型，所有设备通过 DeviceRegistry 注册到 `/dev/`。

```cpp
class Device : public VNode {
    const char* name_; DeviceType type_;  // Char / Block
    virtual int open() / close() / ioctl();
};
class CharDevice : public Device { /* 字节流读写 */ };
class BlockDevice : public Device { /* 扇区读写 */ };
class DeviceRegistry {
    bool register_device(Device* dev);  // 自动挂载到 /dev/<name>
};
```

### MPU 内存保护

Cortex-M4 MPU 8 区域配置，实现用户态/内核态内存隔离。

```cpp
class MPU {
    void configure_region(num, base, size_pow2, ap, execute_never);
    void update_user_sandbox(stack_base, size_pow2);  // PendSV 动态切换
};
// 权限：AP_NO_ACCESS / AP_PRIV_RW / AP_ALL_RW / AP_PRIV_RO / AP_ALL_RO
// Region 0: Flash 全系统只读
// Region 1: RAM 仅特权态读写（用户态 XN 禁止执行）
// Region 2: 当前用户任务栈沙盒（动态）
```

PendSV 上下文切换时调用 `mpu_switch_sandbox()` 动态更新 Region 2。MemManage_Handler 捕获违规访问。内置 `hacker_app_task` 攻击演示验证隔离有效性。

### 显示与输入

**帧缓冲 + 脏区域渲染**（借鉴 BlueOS 超级渲染树）：

```cpp
template<uint16_t W, uint16_t H>
class FrameBuffer {
    void set_pixel(x, y, color);     // 自动标记脏区域
    void fill_rect(x, y, w, h, color); // 自动标记脏区域
    void flush(OledDriver& oled);    // 只推送脏矩形，带宽降 60-97%
};
```

**表盘 Complication 引擎**（借鉴 watchOS）：数据驱动 UI，只有传感器数据变化才触发重绘。

**传感器框架**（借鉴 Zephyr）：统一 `sample_fetch()` + `channel_get()` 接口，支持加速度/心率/计步/电量。

### Lua 小程序引擎

嵌入 Lua 5.4.6 虚拟机，支持脚本开发手表应用（对标 Garmin Connect IQ）。

```cpp
class MiniProgramEngine {
    lua_State* L_;
    // 暴露 C++ API 给 Lua：
    static int api_get_heart_rate(lua_State* L);  // 读取心率
    static int api_draw_text(lua_State* L);       // 绘制文本
    // Lua 脚本可调用传感器、帧缓冲、系统功能
};
```

CMake 配置 `-DLUA_32BITS=1` 适配 32 位平台，裁剪 `lua.c`/`luac.c`/`liolib.c` 等不需要的模块。

### 分布式软总线

借鉴 HarmonyOS，UDP 广播设备发现 + JSON 信标 + 设备路由表。

```cpp
class DistributedSoftBus {
    void init();                // UDP socket + SO_BROADCAST + bind 8899
    void broadcast_beacon();    // 广播 {"event":"beacon","device_id":"aurora_watch_01","cap":["display","touch"]}
    void listener_task();       // 阻塞接收 → JsonParser 解析 → DeviceRouteTable 注册
};

**分布式协议栈安全隔离**：
- **零拷贝 JSON 解析器 OOB 保护**：在 `JsonParser::get_raw_value` 中增加前导字符防越界校验 `*val_start == '\0'`。一旦 JSON 键名后紧跟空字符（例如网络畸形截断报文），解析器将立即中断并快速失败，防止指针跳过空字符读取未授权相邻内存区。
- **设备路由表抗污染**：为 `DeviceRouteTable::register_or_update_device` 添加严格的 `!ip || !dev_id || !cap` 空指针入参校验，确保即使网络层报文解析为空也无法向路由表写入非法数据，保证系统在多设备局域网游牧中的稳定性。
```

---

## 分支结构

auroraOS 采用多分支并行开发策略：

| 分支 | 定位 | 自有代码 | 关键特性 |
|------|------|---------|---------|
| **main** | 核心主线 | 6,014 行 | Lua 小程序 + 意图引擎 + 应用生命周期 + 分布式软总线 |
| **miband** | 小米手环 8 移植 | 7,553 行 | Cortex-M4F + ST7789 + BLE + 手环应用 + 字体引擎 |
| **feature/arm64** | ARM64 探索 | — | 未来 Cortex-A 架构预研 |

main 提供 OS 能力，miband 验证真实硬件落地，两条线并行推进。

---

## 快速开始

### 环境准备

确保已安装以下工具链：

- `arm-none-eabi-gcc` / `arm-none-eabi-g++`（ARM GNU Toolchain）
- `CMake` >= 3.20
- `Make` / `Ninja` / `MinGW32-make`
- `QEMU`（qemu-system-arm，用于 LM3S6965 模拟）
- `Python 3` + `kconfiglib`（用于 Kconfig 配置生成）

```bash
# 安装 kconfiglib
pip install kconfiglib
```

### 构建项目

```bash
git clone --recursive https://github.com/jencaoking/auroraOS.git
cd auroraOS

# 生成 Kconfig 配置（首次必做）
python scripts/genconfig.py

# 构建
mkdir build && cd build
cmake -DBOARD=lm3s6965-qb ..
make -j8
```

### 在 QEMU 中运行

```bash
qemu-system-arm -M lm3s6965evb -nographic -kernel auroraOS.elf
```

启动后进入 `aurora>` 终端。

---

## Shell 命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示可用命令 | `help` |
| `about` | 打印内核版本与架构 | `about` |
| `cat <file>` | 查看文件内容 | `cat /proc/meminfo` |
| `ps` | 显示所有任务状态 | `ps` |
| `free` | 显示内存使用 | `free` |
| `exec <file>` | 执行 ELF 动态应用 | `exec /tmp/app.elf` |
| `ifconfig` | 查看网络接口状态 | `ifconfig` |
| `udpsend <ip> <port> <msg>` | 发送 UDP 数据包 | `udpsend 192.168.1.100 8080 hello` |

---

## 配置系统

auroraOS 采用 Linux 内核风格的 Kconfig 配置系统，支持可裁剪构建。

```bash
# 修改配置
python scripts/menuconfig.bat   # Windows
# 或直接编辑 .config 后重新生成
python scripts/genconfig.py
```

主要配置项：

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BOARD` | string | `lm3s6965-qb` | 目标板级 |
| `SCHEDULER` | bool | y | 启用调度器 |
| `MAX_TASKS` | int | 16 | 最大任务数 |
| `TICK_RATE_HZ` | int | 1000 | SysTick 频率 |
| `VFS` | bool | y | 虚拟文件系统 |
| `FS_RAMFS` | bool | y | RamFS |
| `FS_PROCFS` | bool | y | ProcFS |
| `DEVICE_UART` | bool | y | UART 驱动 |
| `TIMER_MANAGER` | bool | y | 软件定时器 |
| `WORK_QUEUE` | bool | y | 工作队列 |
| `NETWORKING` | bool | y | 网络子系统 |
| `NET_LWIP` | bool | y | lwIP 协议栈 |
| `NET_DEFAULT_IP` | string | `10.0.2.15` | 默认 IP |
| `NET_DEFAULT_MAC` | string | `00:11:22:33:44:55` | 默认 MAC |

---

## 开发路线图

### Phase 1: 内核加固（3-6 个月）— ✅ 98% 完成

- [x] 优先级抢占调度器 + 上下文切换
- [x] 架构抽象层（arch_api → arch_impl）
- [x] 线程安全内核堆 + RAII LockGuard
- [x] Mutex（优先级继承）+ Semaphore + MessageQueue
- [x] POSIX 兼容层（open/read/write/close/ioctl/lseek/sleep/sem_*）
- [x] 设备驱动框架（Device/CharDevice/BlockDevice + DeviceRegistry）
- [x] ProcFS（/proc/meminfo + /proc/taskinfo）
- [x] Shell 增强（ps/free/ifconfig/udpsend）
- [x] 任务通知（TaskNotify）+ POSIX 信号（Signal）
- [x] 信号分发与调度栈隔离安全加固
- [x] ELF 动态加载器安全边界与溢出校验
- [x] 帧感知调度（FrameScheduler）
- [x] 软件定时器（TimerManager）
- [x] 工作队列（WorkQueue）
- [x] Kconfig 配置系统
- [x] lwIP TCP/IP 全栈 + DHCP
- [x] MPU 内存保护（计划外提前实现）
- [x] 以太网驱动防死锁排空机制
- [x] 分布式软总线/JSON解析越界防护
- [x] Shell 命令补全（ping/netstat/reboot/date）
- [ ] irq_save/restore 扩展
- [ ] 消息队列优先级

### Phase 2: 手表原型（6-12 个月）— 🚧 63% 完成

- [x] 帧缓冲 + 脏区域渲染（FrameBuffer）
- [x] OLED 驱动框架（OledDriver）
- [x] 输入事件子系统（InputEvent）
- [x] 触摸驱动（TouchDriver）
- [x] 表盘 UI + Complications（WatchFaceEngine）
- [x] 分级功耗管理（PowerManager，5 级 + Tickless）
- [x] 传感器框架（SensorFramework，Zephyr 风格）
- [x] LittleFS + PhotonCache 光子缓存
- [x] ST7789 真实驱动（miband 分支）
- [x] BLE 协议栈架构（miband 分支）
- [ ] 2D 绘图引擎
- [ ] BLE 协议栈完整移植（Zephyr BLE）
- [ ] Tickless 真实硬件唤醒定时器
- [ ] 充电管理驱动

### Phase 3: 手表智能化（12-18 个月）— 📅 部分提前

- [x] 意图引擎（IntentEngine，规则决策）
- [x] 应用框架 + 生命周期（AppControlBlock）
- [x] 小程序框架（MiniProgramEngine + Lua 5.4.6）
- [x] 分布式软总线（DistributedSoftBus）
- [ ] 运动健康算法框架
- [ ] 通知系统
- [ ] NFC
- [ ] 表盘商店
- [ ] OTA 无线升级
- [ ] 安全启动

### Phase 4: 手机探索（18-36 个月）— 🌌 规划中

- [ ] MMU 虚拟内存（Cortex-A）
- [ ] 进程隔离 + Capability 安全模型（seL4）
- [ ] 消息传递 IPC（QNX 风格）
- [ ] WiFi 驱动 + 完整 TCP/IP
- [ ] GPU 驱动 + GUIX 图形框架
- [ ] 摄像头 + 多媒体
- [ ] 应用沙盒

---

## 借鉴来源

auroraOS 的架构设计站在了巨人的肩膀上，深度借鉴了以下 12 款优秀操作系统：

| 系统 | 维护方 | auroraOS 借鉴重点 |
|------|--------|------------------|
| **小米 Vela (NuttX)** | 小米 | POSIX 兼容、设备驱动框架、ProcFS、优先级继承锁、工作队列 |
| **vivo 蓝河 (BlueOS)** | vivo | 帧感知调度、脏区域渲染、光子存储缓存、意图引擎 |
| **Zephyr** | Linux Foundation | Kconfig 配置系统、传感器框架、BLE 协议栈 |
| **RT-Thread** | 中国开源 | FinSH Shell、设备框架、软件定时器 |
| **watchOS** | Apple | 应用生命周期、表盘 Complications、健康框架 |
| **LiteOS** | 华为 | 低功耗框架、tickless 模式、极小内核 |
| **HarmonyOS Watch** | 华为 | 分布式软总线、原子化服务 |
| **FreeRTOS** | Amazon | 任务通知、软件定时器、tickless 低功耗 |
| **ThreadX (Azure RTOS)** | Microsoft | 消息队列优先级、极低延迟优化 |
| **Garmin OS** | Garmin | 极致低功耗、运动算法、Connect IQ 小程序 |
| **QNX** | BlackBerry | 微内核 IPC、进程隔离、资源管理器 |
| **seL4** | seL4 Foundation | 最小化微内核、capability 安全模型、空间/时间隔离 |

---

## 质量指标

| 指标 | 目标 | 当前状态 |
|------|------|---------|
| 中断响应延迟 | ≤ 10μs | 待量化测量 |
| 上下文切换 | ≤ 5μs | 待量化测量 |
| 手表待机功耗 | ≤ 1mW (AOD) | wfi 已实现，Tickless 待硬件验证 |
| POSIX 兼容 | 50%+ (Phase 1) | 文件/信号量/睡眠已实现，约 40-50% |
| 内核最小 RAM | ≤ 32KB | 远低于 32KB（LiteOS 10KB，BlueOS 13KB）|
| 可移植性 | 换芯片只改 board.h | ✅ 已实现（arch_api + board.h + Kconfig）|

---

## 致谢

auroraOS 的架构设计站在了巨人的肩膀上。感谢上述 12 款操作系统的开源社区与设计者，他们的智慧让 auroraOS 得以在精简代码内实现丰富的 RTOS 能力。

特别感谢以下开源项目：
- **lwIP** — Adam Dunkels 及社区，嵌入式 TCP/IP 协议栈标杆
- **LittleFS** — ARM Limited，掉电安全的小型文件系统
- **Lua** — PUC-Rio，轻量级嵌入式脚本语言

---

## 许可证

本项目采用 [Apache License 2.0](LICENSE) 开源许可证。

第三方依赖保留各自许可证：
- lwIP: BSD-3-Clause
- LittleFS: BSD-3-Clause
- Lua 5.4.6: MIT

---

<div align="center">
  <p><i>auroraOS · 从学习演示到多分支 Lua 化智能手表 RTOS 平台</i></p>
  <p><i>Repository: https://github.com/jencaoking/auroraOS</i></p>
</div>