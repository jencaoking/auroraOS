#ifndef AURORA_DEVICE_HPP
#define AURORA_DEVICE_HPP

#include "vfs.hpp" // 引入我们之前的 VFS 节点定义
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "mutex.hpp"

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
    char* vfs_path_ = nullptr;

public:
    Device(const char* name, DeviceType type) : name_(name), type_(type) {}
    virtual ~Device() {
        if (vfs_path_) free(vfs_path_);
    }

    const char* get_vfs_path() const { return vfs_path_; }
    void set_vfs_path(char* path) { vfs_path_ = path; }

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
private:
    Mutex registry_mutex_;
public:
    static DeviceRegistry& instance() {
        static DeviceRegistry registry;
        return registry;
    }

    // 将设备注册到 /dev/ 路径下
    bool register_device(Device* dev) {
        LockGuard lock(registry_mutex_);
        if (dev->get_vfs_path() != nullptr) return false;
        
        const char* name = dev->get_name();
        int name_len = 0;
        while (name[name_len]) name_len++;
        
        int path_len = 5 + name_len + 1;
        char* path = (char*)malloc(path_len);
        if (!path) return false;
        
        path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
        for (int j = 0; j < name_len; j++) {
            path[5 + j] = name[j];
        }
        path[path_len - 1] = '\0';

        dev->set_vfs_path(path);
        
        // 挂载到我们现有的 VFS 管理器中
        bool res = VfsManager::instance().mount(path, dev);
        if (!res) {
            free(path);
            dev->set_vfs_path(nullptr);
        }
        return res;
    }
};

#endif
