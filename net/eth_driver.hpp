#ifndef ETH_DRIVER_HPP
#define ETH_DRIVER_HPP

#include "net_device.hpp"

class StellarisEth : public NetDevice {
private:
    // LM3S 芯片以太网 MAC 寄存器基地址
    static constexpr uintptr_t MAC_BASE = 0x40048000;
    static constexpr uintptr_t SYSCTL_BASE = 0x400FE000;

    volatile uint32_t* const mac_ris_;  // 中断状态
    volatile uint32_t* const mac_iack_; // 中断清除
    volatile uint32_t* const mac_rctl_; // 接收控制
    volatile uint32_t* const mac_tctl_; // 发送控制
    volatile uint32_t* const mac_data_; // FIFO 数据发送/接收口
    volatile uint32_t* const mac_ia0_;  // MAC 地址低4字节
    volatile uint32_t* const mac_ia1_;  // MAC 地址高2字节

public:
    StellarisEth();
    
    bool init() override;
    int receive_frame(uint8_t* buffer, int max_len) override;
    bool send_frame(const uint8_t* buffer, int len) override;

    static StellarisEth& instance() {
        static StellarisEth eth;
        return eth;
    }
};

#endif
