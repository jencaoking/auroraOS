#include "shell.hpp"
#include "vfs.hpp"
#include "syscall.hpp"
#include "elf_loader.hpp"
#include "config.h"

// 引入 lwIP 的网络接口与 Socket 核心 API
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/api.h"

// 从 kernel.cpp 中引出我们在底层挂载的全局网卡对象
extern struct netif g_netif;

bool Shell::strings_equal(const char* s1, const char* s2) {
    while (*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return (*s1 == '\0' && *s2 == '\0');
}

// 裸机极简版字符串转整数 (atoi)
static int my_atoi(const char* str) {
    int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

void Shell::execute_command(const char* raw_cmd) {
    int stdout_fd = VfsManager::instance().open("/dev/tty0");
    if (stdout_fd < 0) return;

    auto print = [&](const char* str) {
        int len = 0; while(str[len]) len++;
        VfsManager::instance().write(stdout_fd, str, len); // 修正：移除多余的 0
    };

    // 1. 将只读的 raw_cmd 拷贝到本地缓冲区，以便进行就地字符串切割
    char cmd_copy[128];
    int i = 0;
    while (raw_cmd[i] && i < 127) { cmd_copy[i] = raw_cmd[i]; i++; }
    cmd_copy[i] = '\0';

    // 2. 解析 argc 和 argv (按空格分割)
    char* argv[4];
    int argc = 0;
    char* p = cmd_copy;
    
    while (*p && argc < 4) {
        while (*p == ' ') p++;       // 跳过前导空格
        if (!*p) break;
        argv[argc++] = p;            // 记录参数起始地址
        while (*p && *p != ' ') p++; // 寻找单词结尾
        if (*p) { 
            *p = '\0';               // 截断字符串
            p++; 
        }
    }

    if (argc == 0) {
        VfsManager::instance().close(stdout_fd);
        return;
    }

    // ==========================================
    // 3. 命令路由表
    // ==========================================
    if (strings_equal(argv[0], "help")) {
        print("auroraOS built-in commands:\r\n");
        print("  help      - Show this message\r\n");
        print("  cat       - Read data from /tmp/log.txt\r\n");
        print("  about     - Show system information\r\n");
        print("  exec      - Launch dynamic ELF app\r\n");
        print("  ifconfig  - Show network interface status\r\n");
        print("  udpsend   - Send UDP packet <ip> <port> <msg>\r\n");
    } 
    else if (strings_equal(argv[0], "cat")) {
        int fd = VfsManager::instance().open("/tmp/log.txt");
        if (fd >= 0) {
            char buf[64];
            VfsManager::instance().lseek(fd, 0);
            int bytes = VfsManager::instance().read(fd, buf, sizeof(buf)-1);
            if (bytes > 0) {
                buf[bytes] = '\0';
                print(buf);
                print("\r\n");
            }
            VfsManager::instance().close(fd);
        } else {
            print("Failed to open /tmp/log.txt\r\n");
        }
    } 
    else if (strings_equal(argv[0], "about")) {
        print("auroraOS v" KERNEL_VERSION " - Microkernel RTOS\r\n");
        print("Architecture: ARM Cortex-M4\r\n");
    } 
    else if (strings_equal(argv[0], "exec")) {
        print("Launching dynamic application from /tmp/app.elf...\r\n");
        bool success = ElfLoader::load_and_exec("/tmp/app.elf");
        if (success) {
            print(">> Dynamic application loaded into Scheduler successfully!\r\n");
        } else {
            print(">> Failed to load application.\r\n");
        }
    }
    // [网络命令]: 查看网卡状态
    else if (strings_equal(argv[0], "ifconfig")) {
        print("en0   Link encap: Ethernet  HWaddr ");
        
        // 读取底层物理 MAC 地址
        const uint8_t* mac = g_netif.hwaddr;
        const char hex[] = "0123456789ABCDEF";
        for (int k = 0; k < 6; k++) {
            char buf[4] = {hex[mac[k] >> 4], hex[mac[k] & 0xF], (char)((k == 5) ? '\0' : ':'), '\0'};
            print(buf);
        }
        print("\r\n");

        // 调用 lwIP 的 IP 格式化工具打印网卡地址
        print("      inet addr: ");
        print(ip4addr_ntoa(netif_ip4_addr(&g_netif)));
        print("  Mask: ");
        print(ip4addr_ntoa(netif_ip4_netmask(&g_netif)));
        print("  GW: ");
        print(ip4addr_ntoa(netif_ip4_gw(&g_netif)));
        print("\r\n");

        if (netif_is_link_up(&g_netif)) {
            print("      UP BROADCAST RUNNING MULTICAST  MTU:1500\r\n");
        } else {
            print("      DOWN\r\n");
        }
    } 
    // [网络命令]: 通过 lwIP Netconn API 主动发包
    else if (strings_equal(argv[0], "udpsend")) {
        if (argc < 4) {
            print("Usage: udpsend <ip> <port> <msg>\r\n");
        } else {
            ip_addr_t dest_ip;
            ipaddr_aton(argv[1], &dest_ip);    // 解析目标 IP
            int port = my_atoi(argv[2]);       // 解析目标端口

            // 1. 创建 UDP Socket
            struct netconn* conn = netconn_new(NETCONN_UDP);
            if (conn) {
                // 2. 连接到目标主机
                netconn_connect(conn, &dest_ip, port);
                
                // 3. 构建底层网络报文 (netbuf)
                struct netbuf* buf = netbuf_new();
                int msg_len = 0; while(argv[3][msg_len]) msg_len++;
                netbuf_ref(buf, argv[3], msg_len); // 零拷贝引用 Payload

                // 4. 发送数据！
                err_t err = netconn_send(conn, buf);
                if (err == ERR_OK) {
                    print(">> UDP Packet sent successfully via lwIP!\r\n");
                } else {
                    print(">> [Error] Failed to send UDP packet.\r\n");
                }

                // 5. 释放内存资源
                netbuf_delete(buf);
                netconn_delete(conn);
            }
        }
    }
    else {
        print("aurorash: command not found: ");
        print(argv[0]);
        print("\r\n");
    }
    
    VfsManager::instance().close(stdout_fd);
}

void Shell::run() {
    int stdin_fd = VfsManager::instance().open("/dev/tty0");
    if (stdin_fd < 0) return;

    const char* prompt = "aurora> ";
    char cmd_buf[128]; // 增大缓冲区以适应带参数的命令

    while (true) {
        int p_len = 0; while(prompt[p_len]) p_len++;
        VfsManager::instance().write(stdin_fd, prompt, p_len);

        int bytes = VfsManager::instance().read(stdin_fd, cmd_buf, sizeof(cmd_buf) - 1);
        if (bytes > 0) {
            // 将读取到的内容作为字符串
            cmd_buf[bytes] = '\0';
            
            // 剔除末尾可能存在的换行符 \r 或 \n
            while (bytes > 0 && (cmd_buf[bytes-1] == '\r' || cmd_buf[bytes-1] == '\n')) {
                cmd_buf[bytes-1] = '\0';
                bytes--;
            }
            
            execute_command(cmd_buf);
        }
    }
}
