#ifndef AURORA_DEVICE_HPP
#define AURORA_DEVICE_HPP

#include "vfs.hpp" // 引入我们之前的 VFS 节点定义
#include <stdint.h>
#include <stddef.h>

// 设备类型枚举
enum class DeviceType {
    Unknown,
    Char,   // 字符设备（如 UART, SPI, 传感器）
    Block   // 块设备（如 Flash, SD 卡）
};

// 继承自 VNode 的统一 Device 基类
class Device : public VNode {
protected:
    const char* name_;
    DeviceType type_;

public:
    Device(const char* name, DeviceType type) : name_(name), type_(type) {}
    virtual ~Device() = default;

    DeviceType get_type() const { return type_; }
    const char* get_name() const { return name_; }

    // 设备的生命周期与控制接口
    virtual int open() { return 0; }
    virtual int close() { return 0; }
    virtual int ioctl(int /*request*/, void* /*arg*/, void* /*priv*/) override { return -1; }
};

// 字符设备派生类
class CharDevice : public Device {
public:
    CharDevice(const char* name) : Device(name, DeviceType::Char) {}
    
    // 字符设备按字节流读写
    virtual int read(char* /*buf*/, int /*len*/, int /*offset*/, void* /*priv*/) override { return -1; }
    virtual int write(const char* /*buf*/, int /*len*/, int /*offset*/, void* /*priv*/) override { return -1; }
};

// 块设备派生类
class BlockDevice : public Device {
public:
    BlockDevice(const char* name) : Device(name, DeviceType::Block) {}
    
    // 块设备通常需要扇区对齐的读写
    virtual int read_blocks(uint32_t /*block_addr*/, uint32_t /*offset*/, uint8_t* /*buf*/, uint32_t /*size*/) { return -1; }
    virtual int write_blocks(uint32_t /*block_addr*/, uint32_t /*offset*/, const uint8_t* /*buf*/, uint32_t /*size*/) { return -1; }
};

// 设备注册表：负责将设备对象绑定到 VFS
class DeviceRegistry {
public:
    static DeviceRegistry& instance() {
        static DeviceRegistry registry;
        return registry;
    }

    // 将设备注册到 /dev/ 路径下
    bool register_device(Device* dev) {
        // 构造虚拟路径，如 "/dev/uart0"
        char* path = (char*)malloc(32);
        if (!path) return false;
        path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
        int i = 5, j = 0;
        const char* name = dev->get_name();
        while (name[j] && i < 31) {
            path[i++] = name[j++];
        }
        path[i] = '\0';

        // 挂载到我们现有的 VFS 管理器中
        return VfsManager::instance().mount(path, dev);
    }
};

#endif
