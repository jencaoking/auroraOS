#ifndef AURORA_DISTRIBUTED_BUS_HPP
#define AURORA_DISTRIBUTED_BUS_HPP

#include "lwip/sockets.h"
#include "task.hpp"
#include "posix.hpp"
#include <string.h>

class DistributedSoftBus {
private:
    int udp_socket_;
    static constexpr uint16_t SOFTBUS_PORT = 8899; // 软总线专属发现端口

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

        // 构建超级终端设备凭证 (JSON/RPC 风格)
        const char* beacon_payload = "{\"event\":\"beacon\",\"device_id\":\"aurora_watch_01\",\"cap\":[\"display\",\"touch\"]}";
        
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

        while (true) {
            // 阻塞等待局域网内任何设备发向 8899 端口的数据
            int bytes = lwip_recvfrom(udp_socket_, recv_buf, sizeof(recv_buf) - 1, 0,
                                      (struct sockaddr*)&remote_addr, &addr_len);
            
            if (bytes > 0) {
                recv_buf[bytes] = '\0';
                
                char ip_str[16];
                ip4addr_ntoa_r((const ip4_addr_t*)&remote_addr.sin_addr, ip_str, sizeof(ip_str));

                // 打印发现的远端设备信息
                char msg[256];
                int len = 0;
                auto append = [&](const char* s) { while (*s) msg[len++] = *s++; };
                
                append("\r\n\r\n\xE2\x9B\xB0\xEF\xB8\x8F [SoftBus] Device Discovered! IP: ");
                append(ip_str);
                append("\r\n   Payload: ");
                append(recv_buf);
                append("\r\n\r\n");

                write(console_fd, msg, len);

                // 在这里可以解析 payload，把对方设备的 IP 和能力注册到内核的设备发现树中
            }
        }
    }
};

#endif
