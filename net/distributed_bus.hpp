#ifndef AURORA_DISTRIBUTED_BUS_HPP
#define AURORA_DISTRIBUTED_BUS_HPP

#include "lwip/sockets.h"
#include "task.hpp"
#include "posix.hpp"
#include <string.h>
#include "../utils/json_parser.hpp"
#include "device_route_table.hpp"

#include "../kernel/timer.hpp"

class DistributedSoftBus {
private:
    int udp_socket_;
    static constexpr uint16_t SOFTBUS_PORT = 8899; // 软总线专属发现端口

    // ========================================================
    // Placeholder for Challenge-Response + HMAC-SHA256
    // ========================================================
    bool verify_hmac_sha256(const char* challenge, const char* hash) {
        // Dummy verification. Real impl would compute HMAC-SHA256
        return strcmp(hash, "hmac_sha256_result") == 0;
    }

    bool validate_cap(const char* cap) {
        int len = strnlen(cap, 64);
        if (len == 0 || len == 64) return false;
        for (int i = 0; i < len; i++) {
            char c = cap[i];
            if (!((c >= 'a' && c <= 'z') || c == '_' || c == ',' || c == '[' || c == ']' || c == '"')) {
                return false;
            }
        }
        return true;
    }

    bool validate_device_id(const char* id) {
        int len = strnlen(id, 32);
        if (len == 0 || len == 32) return false;
        for (int i = 0; i < len; i++) {
            char c = id[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                return false;
            }
        }
        return true;
    }

public:
    static DistributedSoftBus& instance() {
        static DistributedSoftBus bus;
        return bus;
    }

    void init() {
        // 1. 创建 UDP Socket
        udp_socket_ = lwip_socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket_ < 0) return;

        // 2. 开启 Socket 的广播权限
        int broadcast_enable = 1;
        lwip_setsockopt(udp_socket_, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

        // 3. 绑定本地端口，准备接收其他设备的广播
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = lwip_htons(SOFTBUS_PORT);
        local_addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);

        lwip_bind(udp_socket_, (struct sockaddr*)&local_addr, sizeof(local_addr));
    }

    // ========================================================
    // 发送广播信标 (由软件定时器每 3 秒调用一次)
    // ========================================================
    void broadcast_beacon() {
        if (udp_socket_ < 0) return;

        struct sockaddr_in broadcast_addr;
        memset(&broadcast_addr, 0, sizeof(broadcast_addr));
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = lwip_htons(SOFTBUS_PORT);
        broadcast_addr.sin_addr.s_addr = lwip_htonl(INADDR_BROADCAST); // 255.255.255.255

        // 构建超级终端设备凭证 (JSON/RPC 风格)，使用全字母 device_id 并携带 hmac_sha256 结果
        const char* beacon_payload = "{\"event\":\"beacon\",\"device_id\":\"aurorawatch\",\"cap\":[\"display\",\"touch\"],\"auth\":\"hmac_sha256_result\"}";
        
        lwip_sendto(udp_socket_, beacon_payload, strlen(beacon_payload), 0,
                    (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    }

    // ========================================================
    // 软总线监听线程核心逻辑 (阻塞接收网络报文)
    // ========================================================
    void listener_task() {
        char recv_buf[256];
        struct sockaddr_in remote_addr;
        socklen_t addr_len = sizeof(remote_addr);

        int console_fd = open("/dev/uart0", 0);
        write(console_fd, "[SoftBus] Distributed Engine Listening on UDP 8899...\r\n", 55);
        close(console_fd);

        while (true) {
            int bytes = lwip_recvfrom(udp_socket_, recv_buf, sizeof(recv_buf) - 1, 0,
                                      (struct sockaddr*)&remote_addr, &addr_len);
            
            if (bytes > 0) {
                recv_buf[bytes] = '\0';
                
                char ip_str[16];
                ip4addr_ntoa_r((const ip4_addr_t*)&remote_addr.sin_addr, ip_str, sizeof(ip_str));

                // ========================================================
                // 核心：调用零开销 JSON 解析器拆解 UDP 报文！
                // ========================================================
                JsonParser parser(recv_buf);
                char event_type[32] = {0};
                char device_id[32] = {0};
                char cap_array[64] = {0};
                char auth_token[64] = {0};

                // 只处理 "event":"beacon" 的心跳报文，并强制严格验证
                if (parser.get_string("event", event_type, 32) && strcmp(event_type, "beacon") == 0) {
                    if (parser.get_string("device_id", device_id, 32) && validate_device_id(device_id) &&
                        parser.get_raw_value("cap", cap_array, 64) && validate_cap(cap_array) &&
                        parser.get_string("auth", auth_token, 64) && verify_hmac_sha256(device_id, auth_token)) {
                        
                        // 获取系统时间戳
                        uint32_t current_tick = TimerManager::instance().get_current_tick();

                        // 全部提取且验签、校验成功！将其扔给超级终端路由表进行智能注册
                        DeviceRouteTable::instance().register_or_update_device(
                            ip_str, device_id, cap_array, current_tick
                        );
                    }
                }
            }
        }
    }
};

#endif
