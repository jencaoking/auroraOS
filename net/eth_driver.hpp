#ifndef ETH_DRIVER_HPP
#define ETH_DRIVER_HPP

#include "net_device.hpp"
#include "board.h"

class StellarisEth : public NetDevice {
private:
    // MAC / SYSCTL 基地址统一来自 BSP (boards/<board>/board.h)
    // 驱动不再硬编码任何物理地址，更换芯片只需替换 board.h
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
