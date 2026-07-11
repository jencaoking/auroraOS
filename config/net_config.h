#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdint.h>

// =====================================================================
// 网络协议层配置：将 IP / 掩码 / 网关等可变参数从业务逻辑中剥离
//
// 后续接入 DHCP 时只需两步，业务层 (tcpip_init_done) 无需改动：
//   1. 在 adapter/net/lwipopts.h 中开启 LWIP_DHCP
//   2. 将 default_ipv4_config.use_dhcp 置为 true
// =====================================================================

struct NetIpv4Config {
    uint8_t ip[4];        // 本机 IPv4 地址
    uint8_t netmask[4];   // 子网掩码
    uint8_t gateway[4];   // 默认网关
    bool    use_dhcp;     // true: 由 DHCP 动态获取（需启用 LWIP_DHCP）；false: 使用静态配置
};

// 板级默认静态 IPv4 配置
constexpr NetIpv4Config default_ipv4_config = {
    {192, 168, 1, 100},
    {255, 255, 255, 0},
    {192, 168, 1, 1},
    false
};

#endif // NET_CONFIG_H
