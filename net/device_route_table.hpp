#ifndef AURORA_DEVICE_ROUTE_TABLE_HPP
#define AURORA_DEVICE_ROUTE_TABLE_HPP

#include <stdint.h>
#include "posix.hpp" // 用于使用 printf/write

struct RemoteDevice {
    bool     is_online;
    char     ip_addr[16];     // 设备 IP (如 "10.0.2.15")
    char     device_id[32];   // 解析出的设备名 (如 "aurora_watch_01")
    char     capabilities[64];// 解析出的能力表 (如 "[\"display\",\"touch\"]")
    uint32_t last_seen_tick;  // 最后一次心跳的系统 Tick (用于未来剔除掉线设备)
};

class DeviceRouteTable {
private:
    static constexpr int MAX_DEVICES = 8;
    RemoteDevice devices_[MAX_DEVICES];
    
    // 简易的字符串比较
    bool str_equals(const char* s1, const char* s2) {
        while (*s1 && *s2) {
            if (*s1++ != *s2++) return false;
        }
        return *s1 == *s2;
    }

    // 简易的字符串拷贝
    void str_copy(char* dest, const char* src, int max_len) {
        int i = 0;
        while (*src && i < max_len - 1) dest[i++] = *src++;
        dest[i] = '\0';
    }

public:
    static DeviceRouteTable& instance() {
        static DeviceRouteTable table;
        return table;
    }

    DeviceRouteTable() {
        for (int i = 0; i < MAX_DEVICES; i++) devices_[i].is_online = false;
    }

    // ========================================================
    // 将解析出的新设备动态注册到内核路由表中
    // ========================================================
    void register_or_update_device(const char* ip, const char* dev_id, const char* cap, uint32_t current_tick) {
        int empty_slot = -1;

        // 1. 查重：如果设备已存在，更新心跳时间
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (devices_[i].is_online && str_equals(devices_[i].device_id, dev_id)) {
                devices_[i].last_seen_tick = current_tick;
                // 若 IP 发生游牧漫游变化，动态更新
                if (!str_equals(devices_[i].ip_addr, ip)) {
                    str_copy(devices_[i].ip_addr, ip, 16);
                }
                return; // 更新完毕，直接返回
            }
            if (!devices_[i].is_online && empty_slot == -1) {
                empty_slot = i;
            }
        }

        // 2. 发现全新设备！填入空槽位
        if (empty_slot != -1) {
            devices_[empty_slot].is_online = true;
            str_copy(devices_[empty_slot].ip_addr, ip, 16);
            str_copy(devices_[empty_slot].device_id, dev_id, 32);
            str_copy(devices_[empty_slot].capabilities, cap, 64);
            devices_[empty_slot].last_seen_tick = current_tick;

            // 触发炫酷的内核控制台提示
            int fd = open("/dev/uart0", 0);
            if (fd >= 0) {
                char msg[256]; int len = 0;
                auto append = [&](const char* s) { while (*s) msg[len++] = *s++; };
                
                append("\r\n\xF0\x9F\x93\xB1 [Super Terminal] NEW Device Registered!\r\n");
                append("   => ID: "); append(dev_id);
                append("\r\n   => IP: "); append(ip);
                append("\r\n   => Capabilities: "); append(cap); append("\r\n\r\n");
                
                write(fd, msg, len);
                close(fd);
            }
        }
    }
};

#endif
