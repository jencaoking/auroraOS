#include "eth_driver.hpp"
#include "mutex.hpp"
#include "syscall.hpp"

extern Mutex uart_mutex;

StellarisEth::StellarisEth()
    : mac_ris_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x000)),
      mac_iack_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x000)),
      mac_rctl_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x008)),
      mac_tctl_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x00C)),
      mac_data_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x018)),
      mac_ia0_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x01C)),
      mac_ia1_(reinterpret_cast<uint32_t*>(MAC_BASE + 0x020)) {
    
    // 为我们的虚拟网卡配置一个好记的默认 MAC: 52:54:00:12:34:56 (QEMU 常用厂商前缀)
    mac_address_[0] = 0x52; mac_address_[1] = 0x54; mac_address_[2] = 0x00;
    mac_address_[3] = 0x12; mac_address_[4] = 0x34; mac_address_[5] = 0x56;
}

bool StellarisEth::init() {
    sys_print("[NetDriver] Activating Stellaris Ethernet Controller Clocks...\r\n");

    // 1. 开启系统控制寄存器中的以太网 MAC 和 PHY 时钟门控 (SYSCTL_RCGC2_R @ 0x400FE108)
    volatile uint32_t* sysctl_rcgc2 = reinterpret_cast<uint32_t*>(SYSCTL_BASE + 0x108);
    *sysctl_rcgc2 |= (1 << 28) | (1 << 30); // Bit 28: MAC Clock, Bit 30: PHY Clock
    
    // 简单循环等待时钟稳定
    for (volatile int i = 0; i < 10000; i++);

    // 2. 写入本机 MAC 地址到硬件过滤寄存器
    *mac_ia0_ = (mac_address_[3] << 24) | (mac_address_[2] << 16) | (mac_address_[1] << 8) | mac_address_[0];
    *mac_ia1_ = (mac_address_[5] << 8) | mac_address_[4];

    // 3. 配置接收控制寄存器 (RCTL):
    // Bit 0: RXEN (开启接收), Bit 1: AMUL (接收多播), Bit 2: PRMS (混杂模式，捕获局域网所有包)
    *mac_rctl_ = (1 << 0) | (1 << 1) | (1 << 2);

    // 4. 配置发送控制寄存器 (TCTL):
    // Bit 0: TXEN (开启发包), Bit 1: PADEN (自动补齐到60字节), Bit 2: CRC (自动追加硬件 CRC 校验和)
    *mac_tctl_ = (1 << 0) | (1 << 1) | (1 << 2);

    link_up_ = true;
    sys_print("[NetDriver] L2 Ethernet MAC Initialized: 52:54:00:12:34:56 [UP]\r\n");
    return true;
}

// 从网卡硬件 FIFO 读取以太网帧
int StellarisEth::receive_frame(uint8_t* buffer, int max_len) {
    // 检查是否有数据帧到达 (读取 MAC_RIS 的 Bit 0: RXINT)
    if ((*mac_ris_ & 0x01) == 0) {
        return 0; // FIFO 为空
    }

    // Stellaris 接收 FIFO 的第一个字是：帧长度 (减去 CRC 的实际数据大小)
    uint32_t frame_len = *mac_data_;
    if (frame_len > static_cast<uint32_t>(max_len) || frame_len == 0) {
        // 数据帧异常，清除接收中断标志并丢弃
        *mac_iack_ = 0x01;
        return 0;
    }

    // 按 4 字节 (Word) 从 FIFO 循环读取并填入缓冲区
    uint32_t* dest_words = reinterpret_cast<uint32_t*>(buffer);
    int words_to_read = (frame_len + 3) / 4;
    for (int i = 0; i < words_to_read; i++) {
        dest_words[i] = *mac_data_;
    }

    // 清除硬件接收中断标志位
    *mac_iack_ = 0x01;
    return static_cast<int>(frame_len);
}

// 将以太网帧写入网卡硬件 FIFO 并触发发包
bool StellarisEth::send_frame(const uint8_t* buffer, int len) {
    if (!link_up_ || len <= 0 || len > 1514) return false;

    // Stellaris 发送 FIFO 要求先写入两字节的长度，再写入数据
    // 为兼容 32 位对齐写，我们先将长度和前两字节拼凑在一起写入
    *mac_data_ = (len & 0xFFFF) | ((buffer[0] << 16) & 0xFF0000) | ((buffer[1] << 24) & 0xFF000000);

    // 把余下的数据按字写入 FIFO
    const uint8_t* remaining_data = buffer + 2;
    int remaining_len = len - 2;
    int words_to_write = (remaining_len + 3) / 4;
    
    for (int i = 0; i < words_to_write; i++) {
        uint32_t word = 0;
        for (int b = 0; b < 4 && (i * 4 + b) < remaining_len; b++) {
            word |= (static_cast<uint32_t>(remaining_data[i * 4 + b]) << (b * 8));
        }
        *mac_data_ = word;
    }

    // 触发网卡发送引擎开始把 FIFO 数据送上物理网线！
    // 取反再重写 Bit 0 (TXEN) 会激活硬件发包脉冲
    *mac_tctl_ |= (1 << 0);
    return true;
}
