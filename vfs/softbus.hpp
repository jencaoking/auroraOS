#ifndef SOFTBUS_HPP
#define SOFTBUS_HPP

#include <stdint.h>

// 预定义总线上最大支持的 RPC 服务数量
constexpr int MAX_RPC_HANDLERS = 5;

// RPC 回调函数签名
using RpcCallback = void (*)(const char* payload);

struct RpcNode {
    const char* cmd;
    RpcCallback handler;
};

class SoftBus {
public:
    static SoftBus& instance() {
        static SoftBus bus;
        return bus;
    }

    void init();
    
    // 注册暴露给总线的服务
    bool register_service(const char* cmd, RpcCallback handler);
    
    // 向总线发送结构化请求
    void send_request(const char* cmd, const char* payload);
    
    // 轮询总线数据（放在后台守护线程中）
    void poll();

private:
    SoftBus() = default;
    RpcNode services_[MAX_RPC_HANDLERS];
    int service_count_ = 0;

    void dispatch(const char* cmd, const char* payload);
    bool strings_equal(const char* s1, const char* s2);
};

#endif
