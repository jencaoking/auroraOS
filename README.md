<div align="center">
  <img src="https://via.placeholder.com/120x120.png?text=auroraOS" alt="auroraOS Logo" width="120" height="120" />
  <h1>auroraOS</h1>
  <p><b>面向手机与手表的微内核实时操作系统</b></p>

  [![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
  [![Platform](https://img.shields.io/badge/Platform-ARM%20Cortex--M4-brightgreen.svg)]()
  [![Network](https://img.shields.io/badge/Network-lwIP%20TCP%2FIP-orange.svg)]()
  [![Storage](https://img.shields.io/badge/Storage-LittleFS%20%2B%20PhotonCache-purple.svg)]()
  [![UI](https://img.shields.io/badge/UI-FrameScheduler%20%2B%20DirtyRect-red.svg)]()
</div>

<br/>

**auroraOS** 是一个极致精简、高性能的微内核实时操作系统（RTOS），基于 ARM Cortex-M4 架构，专为现代智能手表和物联网终端设计。系统深度借鉴了业界 12 款顶尖操作系统（如 小米 Vela、vivo BlueOS、Zephyr、watchOS 等）的设计哲学，实现了极致的性能与优雅的架构。

---

## ✨ 核心特性 (Features)

* 🚀 **极速微内核调度**：5 级优先级抢占式调度，支持同级 Round-Robin 轮转。两阶段 O(N) 算法实现微秒级上下文切换。
* 🎨 **蓝河超级渲染树 (Dirty Rect)**：独创脏区域包围盒渲染引擎，仅重绘和传输屏幕变化的最小区域，SPI 带宽骤降 60%~97%，带来丝滑的 60fps 拖拽体验。
* 💾 **光子存储缓存层 (Photon Cache Layer)**：基于内存页池与 LRU 的写聚合引擎，拦截并聚合微小字节写操作，结合 **LittleFS** 日志文件系统，实现 `0 擦写磨损` 与断电防砖。
* 📦 **ELF 动态加载器**：支持从 VFS 直接加载标准的 ARM Thumb ELF 可执行文件，实现类似 Linux 的动态应用分发，无需重新编译内核。
* 🌐 **全栈网络支持**：深度集成 **lwIP 2.x**，支持 IPv4、TCP、UDP、ICMP、ARP 及标准的 BSD Socket/Netconn API，自带零汇编的 OSAL 适配层。
* 🛡️ **极致的抽象与解耦**：
  * **arch_api**: 内核代码 100% 零汇编，HAL 层高度抽象。
  * **设备框架**: 类 Linux `/dev` VFS 挂载点，支持 POSIX 标准 `open/read/write/ioctl`。
  * **输入子系统**: 屏蔽 I2C/SPI 底层差异，提供标准化的 `InputEvent` 坐标流。

## 🏗️ 系统架构 (Architecture)

```text
┌──────────────────────────────────────────────────────────────────────────────────┐
│                            应用层 (Applications)                                 │
│   Shell 终端  │  UDP Echo Server  │  ELF 动态加载器  │  UI 渲染引擎 / 表盘   │
├──────────────────────────────────────────────────────────────────────────────────┤
│                           网络协议栈 (lwIP)                                      │
│   Socket API  │  Netconn API  │  TCP  │  UDP  │  IPv4  │  ARP  │  ICMP     │
├──────────────────────────────────────────────────────────────────────────────────┤
│                       虚拟文件系统 (VFS) & 存储                                  │
│   VfsManager  │  RamFS  │  LittleFS + 光子缓存层 (Photon)  │  /dev 设备树    │
├──────────────────────────────────────────────────────────────────────────────────┤
│                          微内核 (Kernel)                                          │
│   Scheduler / FrameScheduler (30FPS 帧感知) │  KernelHeap (首次适配+合并)        │
│   Mutex(PI)   │  Semaphore  │  MessageQueue │  Signal / Task Notify          │
├──────────────────────────────────────────────────────────────────────────────────┤
│                       架构抽象层 (arch_api)                                       │
│   disable_interrupts()  │  trigger_context_switch()  │  start_first_task()    │
├──────────────────────────────────────────────────────────────────────────────────┤
│                              硬件层 (Hardware)                                    │
│   ARM Cortex-M4 (LM3S6965) │ 256KB Flash │ 64KB RAM │ Ethernet │ SPI OLED   │
└──────────────────────────────────────────────────────────────────────────────────┘
```

## 🗺️ 开发路线图 (Roadmap)

### Phase 1: 内核加固 (Completed ✅)
- [x] 优先级抢占调度器 & 上下文切换
- [x] 架构抽象层 (arch_api)
- [x] lwIP TCP/IP 集成
- [x] 设备驱动框架 & POSIX 兼容层
- [x] ProcFS 状态导出
- [x] 优先级继承锁 (PI Mutex)
- [x] 任务通知 (Task Notify) & POSIX 信号
- [x] 裸机 libc 桩实现

### Phase 2: 手表原型 (In Progress 🚀)
- [x] 帧感知调度 (Frame-Aware Scheduler)
- [x] 脏区域渲染树 (Dirty Rectangle Super Render Tree)
- [x] SPI OLED 驱动底层框架
- [x] I2C 触控驱动与输入事件子系统 (Input Event Subsystem)
- [x] 光子存储缓存层 (Photon Cache Layer) & LittleFS
- [ ] 蓝牙 BLE 协议栈移植 (Zephyr)
- [ ] 分级功耗管理 & Tickless 模式

### Phase 3: 手表智能化 (Planned 📅)
- [ ] 运动健康算法框架
- [ ] 意图引擎 (Intent Engine)
- [ ] 应用框架与小程序引擎
- [ ] OTA 无线固件升级

### Phase 4: 手机探索 (Future 🌌)
- [ ] MMU 虚拟内存与进程隔离 (Cortex-A)
- [ ] 完整 Capability 安全模型 (参考 seL4)
- [ ] GPU 加速驱动

## 🔨 快速开始 (Getting Started)

### 1. 环境准备
确保已安装以下工具链：
- `arm-none-eabi-gcc` (ARM GNU Toolchain)
- `CMake` (>= 3.20)
- `Make` / `Ninja` / `MinGW32-make`
- `QEMU` (qemu-system-arm)

### 2. 构建项目
```bash
git clone https://github.com/jencaoking/auroraOS.git
cd auroraOS
mkdir build && cd build
cmake -DBOARD=lm3s6965-qb ..
make -j8
```

### 3. 在 QEMU 中运行
```bash
qemu-system-arm -M lm3s6965evb -nographic -kernel auroraOS.elf
```

### 4. 探索 Shell
启动后进入 `aurora>` 终端，支持以下命令：
- `help`: 显示可用命令
- `about`: 打印内核版本与横幅
- `cat <file>`: 查看文件内容（如 `/proc/meminfo`）
- `exec <file>`: 执行 ELF 动态应用
- `ifconfig`: 查看网络接口状态

## 📖 致谢 (Acknowledgments)

auroraOS 的架构设计站在了巨人的肩膀上，深度借鉴了以下优秀操作系统的思想：
* **小米 Vela (NuttX)**: POSIX 接口标准、ProcFS、驱动框架
* **vivo 蓝河 (BlueOS)**: 帧感知调度、光子存储层、超级渲染树
* **Zephyr**: 设备树模型、Kconfig 思想
* **RT-Thread**: FinSH 终端设计
* **FreeRTOS / ThreadX**: 极简 IPC、Tickless 低功耗
* **watchOS / Garmin OS**: 健康模型与极限界能
* **seL4 / QNX**: 微内核隔离与安全性

## 📄 许可证 (License)
本项目采用 [Apache License 2.0](LICENSE) 开源许可证。
