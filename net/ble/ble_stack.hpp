#ifndef AURORA_BLE_STACK_HPP
#define AURORA_BLE_STACK_HPP

#include <stdint.h>
#include <stddef.h>

// ========================================================
// BLE 基础数据结构与权限定义
// ========================================================
enum class BleConnectionState : uint8_t {
    DISCONNECTED,
    ADVERTISING,
    CONNECTED
};

enum class GattPerm : uint8_t {
    READ          = 0x01,
    WRITE         = 0x02,
    NOTIFY        = 0x10,
    INDICATE      = 0x20,
    READ_WRITE    = 0x03,
    NOTIFY_READ   = 0x11
};

inline GattPerm operator|(GattPerm a, GattPerm b) {
    return static_cast<GattPerm>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// ========================================================
// GATT 属性定义 (GattAttribute)
// ========================================================
class GattAttribute {
public:
    uint16_t  uuid_16;       // 16-bit 蓝牙 SIG 标准 UUID (如 0x2A37 为心率测量)
    GattPerm  permissions;   // 读写/通知权限
    uint8_t*  data_ptr;      // 指向真实数据的指针
    uint16_t  data_len;      // 数据长度
    uint16_t  max_len;       // 缓冲区最大长度

    GattAttribute() : uuid_16(0), permissions(GattPerm::READ), data_ptr(nullptr), data_len(0), max_len(0) {}

    void init(uint16_t uuid, GattPerm perm, uint8_t* data, uint16_t len, uint16_t max) {
        uuid_16 = uuid;
        permissions = perm;
        data_ptr = data;
        data_len = len;
        max_len = max;
    }
    
    // 供底层 HCI 在收到远端 Write Request 时调用更新数据
    bool update_data(const uint8_t* new_data, uint16_t len) {
        if (len > max_len || !data_ptr) return false;
        for (uint16_t i = 0; i < len; ++i) {
            data_ptr[i] = new_data[i];
        }
        data_len = len;
        return true;
    }
};

// ========================================================
// GATT 服务定义 (GattService)
// ========================================================
class GattService {
private:
    static constexpr int MAX_ATTRS_PER_SERVICE = 5;
    uint16_t      service_uuid_;
    GattAttribute attributes_[MAX_ATTRS_PER_SERVICE];
    uint8_t       attr_count_;

public:
    GattService() : service_uuid_(0), attr_count_(0) {}

    void init(uint16_t uuid) {
        service_uuid_ = uuid;
        attr_count_ = 0;
    }

    bool add_attribute(uint16_t uuid, GattPerm perm, uint8_t* data, uint16_t len, uint16_t max) {
        if (attr_count_ >= MAX_ATTRS_PER_SERVICE) return false;
        attributes_[attr_count_].init(uuid, perm, data, len, max);
        attr_count_++;
        return true;
    }
    
    uint16_t get_uuid() const { return service_uuid_; }
    uint8_t get_attr_count() const { return attr_count_; }
    GattAttribute* get_attribute(uint8_t index) {
        if (index >= attr_count_) return nullptr;
        return &attributes_[index];
    }
};

// ========================================================
// BLE 核心协议栈管理器 (BleManager)
// ========================================================
class BleManager {
private:
    BleConnectionState current_state_;
    
    // 预定义 5 个标准服务 (对齐 MIBAND 适配报告)
    GattService svc_device_info_; // Device Information Service (0x180A)
    GattService svc_time_;        // Current Time Service (0x1805)
    GattService svc_heart_rate_;  // Heart Rate Service (0x180D)
    GattService svc_battery_;     // Battery Service (0x180F)
    GattService svc_vendor_;      // 厂商自定义服务 (用于私有协议与 OTA)

    // 本地硬件数据缓存映射
    uint8_t  battery_level_;
    uint8_t  hr_measurement_[2]; // [0]: Flags, [1]: BPM
    uint8_t  vendor_rx_buf_[20]; // 接收手机 App 发送的控制指令

    BleManager() : current_state_(BleConnectionState::DISCONNECTED), battery_level_(100) {
        hr_measurement_[0] = 0x00; // 8-bit Heart Rate format
        hr_measurement_[1] = 0;
    }

public:
    static BleManager& instance() {
        static BleManager manager;
        return manager;
    }

    // ========================================================
    // 协议栈初始化与服务注册
    // ========================================================
    void init() {
        // 1. 注册电池服务
        svc_battery_.init(0x180F);
        svc_battery_.add_attribute(0x2A19, GattPerm::READ | GattPerm::NOTIFY, &battery_level_, 1, 1);

        // 2. 注册心率服务
        svc_heart_rate_.init(0x180D);
        svc_heart_rate_.add_attribute(0x2A37, GattPerm::NOTIFY, hr_measurement_, 2, 2);

        // 3. 注册厂商自定义控制服务
        svc_vendor_.init(0xFFE0); // 假定私有服务 UUID 为 0xFFE0
        svc_vendor_.add_attribute(0xFFE1, GattPerm::WRITE | GattPerm::NOTIFY, vendor_rx_buf_, 0, sizeof(vendor_rx_buf_));

        // 此处应调用 Apollo3 厂商 SDK 的 API，将上述 GATT 树下发至底层控制器的 RAM 中
        // hci_register_services(...);
    }

    // ========================================================
    // 连接与广播控制
    // ========================================================
    void start_advertising() {
        if (current_state_ == BleConnectionState::CONNECTED) return;
        
        // 构建广播包 (包含设备名称和主推服务)
        // uint8_t adv_data[] = { 0x02, 0x01, 0x06, 0x0B, 0x09, 'a','u','r','o','r','a','W','A','T','C','H' };
        // hci_set_adv_data(adv_data, sizeof(adv_data));
        // hci_start_advertising();
        
        current_state_ = BleConnectionState::ADVERTISING;
    }

    void stop_advertising() {
        // hci_stop_advertising();
        current_state_ = BleConnectionState::DISCONNECTED;
    }

    // 由底层 HCI 硬件中断回调更新状态
    void on_connected() {
        current_state_ = BleConnectionState::CONNECTED;
    }

    void on_disconnected() {
        current_state_ = BleConnectionState::DISCONNECTED;
        start_advertising(); // 断开后自动重新广播
    }

    BleConnectionState get_state() const { return current_state_; }

    // ========================================================
    // 上层应用数据推送接口 (Notify)
    // ========================================================
    
    // 更新心率并通过蓝牙实时推送到手机
    void update_heart_rate(uint8_t bpm) {
        hr_measurement_[1] = bpm;
        if (current_state_ == BleConnectionState::CONNECTED) {
            // 触发底层 Notify 发送
            // hci_send_notification(svc_heart_rate_.get_uuid(), 0x2A37, hr_measurement_, 2);
        }
    }

    // 更新电池电量
    void update_battery_level(uint8_t level) {
        if (level > 100) level = 100;
        battery_level_ = level;
        if (current_state_ == BleConnectionState::CONNECTED) {
            // hci_send_notification(svc_battery_.get_uuid(), 0x2A19, &battery_level_, 1);
        }
    }
};

#endif // AURORA_BLE_STACK_HPP
