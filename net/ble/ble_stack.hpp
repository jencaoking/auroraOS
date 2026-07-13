#ifndef AURORA_BLE_STACK_HPP
#define AURORA_BLE_STACK_HPP

#include <stdint.h>
#include <string.h>
#include "../../kernel/task.hpp"
#include "../../kernel/msg_queue.hpp"
#include "posix.hpp" // For open/write/close logging

// ========================================================
// 蓝牙连接状态机
// ========================================================
enum class BleConnectionState : uint8_t {
    DISCONNECTED, // 断开连接，仅维持极低频的广播 (Advertising)
    ADVERTISING,  // 高频广播中 (等待手机连接)
    CONNECTING,   // 正在建立 LL 层连接
    CONNECTED     // 链路已建立，GATT 数据通道开启
};

// ========================================================
// 标准 SIG GATT 服务 UUID 定义 (16-bit)
// ========================================================
constexpr uint16_t GATT_SVC_DEVICE_INFO = 0x180A; // 设备信息服务
constexpr uint16_t GATT_SVC_HEART_RATE  = 0x180D; // 心率服务
constexpr uint16_t GATT_SVC_BATTERY     = 0x180F; // 电池服务

// 极客专属：AuroraOS 自定义 OTA 与 Lua 脚本传输服务 (128-bit UUID)
// 比如: 0000FF01-0000-1000-8000-00805F9B34FB
constexpr uint16_t GATT_SVC_AURORA_CUSTOM = 0xFF01; 

// ========================================================
// 蓝牙底层硬件事件定义 (用于解耦芯片厂 SDK 的 HCI 层)
// ========================================================
struct BleHciEvent {
    uint8_t event_type;
    uint16_t connection_handle;
    uint8_t payload[16];
};

class BleManager {
private:
    BleConnectionState current_state_;
    
    // 异步消息队列：用于接收来自底层硬件中断的 HCI 事件，防止阻塞射频中断
    MessageQueue<BleHciEvent, 16> hci_event_queue_;
    
    // 缓存最新的设备特征值，当手机发起 Read 请求时直接返回
    uint8_t cached_battery_level_;
    uint8_t cached_heart_rate_;

    BleManager() : current_state_(BleConnectionState::DISCONNECTED), 
                   cached_battery_level_(100), cached_heart_rate_(0) {}

    // 内部注册所有的 GATT Services 和 Characteristics
    void build_gatt_profile() {
        // 伪代码：向底层 SDK 注册 GATT 数据库
        // 1. 注册 0x180A 设备信息 (包含 Manufacturer Name, Model Number 等)
        // 2. 注册 0x180D 心率服务 (设定 Characteristic 为 Notify 属性)
        // 3. 注册 0x180F 电池服务 (设定 Characteristic 为 Read/Notify 属性)
        // 4. 注册 0xFF01 Aurora 自定义服务 (设定为 Security Mode 1 Level 3 配对后可写，用于接收 Lua 脚本)
    }

public:
    static BleManager& instance() {
        static BleManager manager;
        return manager;
    }

    void init() {
        hci_event_queue_.init();
        build_gatt_profile();
        current_state_ = BleConnectionState::ADVERTISING;
        
        // 伪代码：调用 Ambiq Cordio SDK 等底层库，开启射频广播
        // HalBle::start_advertising("Aurora_MiBand8");
    }

    BleConnectionState get_state() const { return current_state_; }

    // ========================================================
    // 上层应用调用：推送最新状态 (解耦调用，零阻塞)
    // ========================================================
    void update_heart_rate(uint8_t bpm) {
        if (cached_heart_rate_ == bpm) return;
        cached_heart_rate_ = bpm;
        
        // 如果当前是连接状态，且手机订阅了 Notify，则推向空中接口
        if (current_state_ == BleConnectionState::CONNECTED) {
            // HalBle::notify_characteristic(GATT_SVC_HEART_RATE, ... , &bpm, 1);
        }
    }

    void update_battery_level(uint8_t level) {
        if (cached_battery_level_ == level) return;
        cached_battery_level_ = level;

        if (current_state_ == BleConnectionState::CONNECTED) {
            // HalBle::notify_characteristic(GATT_SVC_BATTERY, ... , &level, 1);
        }
    }

    // ========================================================
    // 底层硬件中断钩子 (ISR)：严禁执行任何阻塞操作！
    // ========================================================
    void on_hci_hardware_event_isr(uint8_t event_type, uint16_t handle) {
        BleHciEvent event = {event_type, handle, {0}};
        // 使用非阻塞的 try_push 塞入队列
        hci_event_queue_.try_push_urgent(event); 
    }

    // ========================================================
    // BLE 守护线程执行中枢 (运行在 HIGH 优先级)
    // ========================================================
    void daemon_task() {
        while (true) {
            // 0 功耗挂起，等待底层射频芯片发来事件
            BleHciEvent event = hci_event_queue_.pop();

            Arch::disable_interrupts();
            // 处理连接与断开的逻辑状态机
            switch (event.event_type) {
                case 0x01: // EVENT_CONNECT
                    current_state_ = BleConnectionState::CONNECTED;
                    break;
                case 0x02: // EVENT_DISCONNECT
                    current_state_ = BleConnectionState::ADVERTISING;
                    // 断开后立刻重启广播
                    // HalBle::start_advertising(...);
                    break;
                case 0x03: { // EVENT_DATA_RECEIVED (例如收到手机传来的 Lua 小程序数据包)
                    // ========================================================
                    // 安全加固: 对接收到的 Lua 代码数据进行签名验签，防止执行恶意代码
                    // ========================================================
                    bool signature_valid = false;
                    // TODO: 调用实际的 Crypto 库对 payload 进行验签 (如 ECDSA-SHA256)
                    // 只有持有正确私钥的设备发来的代码才被执行
                    // signature_valid = Crypto::verify_ecdsa(event.payload, ...);
                    
                    if (signature_valid) {
                        // 将收到的分片数据扔给 VFS 或 MiniProgramEngine 处理
                    } else {
                        // 记录安全告警，丢弃非法请求
                        int fd = open("/dev/uart0", 0);
                        if (fd >= 0) {
                            write(fd, "[BLE] Security Alert: Invalid Lua signature!\r\n", 46);
                            close(fd);
                        }
                    }
                    break;
                }
            }
            Arch::enable_interrupts();
        }
    }
};

#endif // AURORA_BLE_STACK_HPP
