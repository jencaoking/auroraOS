#include "softbus.hpp"
#include "uart.h"

void SoftBus::init() {
    service_count_ = 0;
}

// 极其精简的字符串比较（因为裸机环境没有 <string.h>）
bool SoftBus::strings_equal(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

bool SoftBus::register_service(const char* cmd, RpcCallback handler) {
    if (service_count_ >= MAX_RPC_HANDLERS) return false;
    services_[service_count_].cmd = cmd;
    services_[service_count_].handler = handler;
    service_count_++;
    return true;
}

void SoftBus::send_request(const char* cmd, const char* payload) {
    uart_puts("$RPC,");
    uart_puts(cmd);
    uart_puts(",");
    uart_puts(payload);
    uart_puts("#\n");
}

void SoftBus::dispatch(const char* cmd, const char* payload) {
    for (int i = 0; i < service_count_; i++) {
        if (strings_equal(services_[i].cmd, cmd)) {
            // 匹配成功，触发远程调用
            services_[i].handler(payload);
            return;
        }
    }
    // 未知服务
    uart_puts("[SoftBus] Unknown Service requested: ");
    uart_puts(cmd);
    uart_puts("\n");
}

// 状态机解析帧格式: $RPC,CMD,PAYLOAD#
void SoftBus::poll() {
    static int state = 0;
    static char cmd_buf[16];
    static char payload_buf[64];
    static int cmd_idx = 0;
    static int pay_idx = 0;

    char c;
    if (!uart_getc_nb(&c)) return; // 总线无数据，立即让出 CPU

    switch (state) {
        case 0: if (c == '$') state = 1; break;
        case 1: if (c == 'R') state = 2; else state = 0; break;
        case 2: if (c == 'P') state = 3; else state = 0; break;
        case 3: if (c == 'C') state = 4; else state = 0; break;
        case 4: 
            if (c == ',') { state = 5; cmd_idx = 0; } 
            else state = 0; 
            break;
        case 5: // 提取指令标识
            if (c == ',') {
                cmd_buf[cmd_idx] = '\0';
                state = 6;
                pay_idx = 0;
            } else if (cmd_idx < 15) {
                cmd_buf[cmd_idx++] = c;
            }
            break;
        case 6: // 提取数据负载
            if (c == '#') {
                payload_buf[pay_idx] = '\0';
                dispatch(cmd_buf, payload_buf); // 派发执行
                state = 0; 
            } else if (pay_idx < 63) {
                payload_buf[pay_idx++] = c;
            }
            break;
        default: state = 0; break;
    }
}
