#include "ramfs.hpp"
#include <string.h> // for memcpy, memset
#ifndef AURORA_HOST_TEST
#include "autoconf.h"
#endif

// 依赖全局重载的 operator new
RamFile::RamFile(int capacity) : capacity_(capacity), file_size_(0) {
#ifdef CONFIG_NO_DYNAMIC_ALLOCATION
    static char ramfs_buffers[4][1024]; // Support max 4 files of 1KB each
    static int next_buf = 0;
    if (next_buf < 4 && capacity <= 1024) {
        data_ = ramfs_buffers[next_buf++];
        capacity_ = 1024;
    } else {
        data_ = nullptr;
        capacity_ = 0;
    }
#else
    data_ = new char[capacity_];
#endif
    if (data_) {
        memset(data_, 0, capacity_);
    } else {
        capacity_ = 0;
    }
}

RamFile::~RamFile() {
#ifndef CONFIG_NO_DYNAMIC_ALLOCATION
    delete[] data_;
#endif
}

int RamFile::read(char* buf, int len, int offset, void* priv) {
    if (!data_ || offset >= file_size_) return 0; // EOF (End of File)
    
    int bytes_to_read = len;
    if (len > file_size_ - offset) {
        bytes_to_read = file_size_ - offset;
    }

    memcpy(buf, data_ + offset, bytes_to_read);
    return bytes_to_read;
}

int RamFile::write(const char* buf, int len, int offset, void* priv) {
    // 如果写入超出了当前物理容量，尝试动态扩容
    if (offset + len > capacity_) {
#ifdef CONFIG_NO_DYNAMIC_ALLOCATION
        // 扩容失败，截断写入
        if (offset >= capacity_) return 0; // 完全无法写入
        len = capacity_ - offset;
#else
        int new_capacity = capacity_ == 0 ? 512 : capacity_ * 2;
        while (new_capacity < offset + len) {
            new_capacity *= 2;
        }
        
        char* new_data = new char[new_capacity];
        if (!new_data) {
            // 扩容失败，截断写入
            if (offset >= capacity_) return 0; // 完全无法写入
            len = capacity_ - offset;
        } else {
            // 扩容成功，拷贝旧数据并初始化新空间
            if (data_) {
                memcpy(new_data, data_, capacity_);
                delete[] data_;
            }
            memset(new_data + capacity_, 0, new_capacity - capacity_);
            data_ = new_data;
            capacity_ = new_capacity;
        }
#endif
    }

    int bytes_to_write = len;
    if (data_) {
        memcpy(data_ + offset, buf, bytes_to_write);
    } else {
        return 0; // No memory available
    }

    // 动态更新文件逻辑大小
    if (offset + bytes_to_write > file_size_) {
        file_size_ = offset + bytes_to_write;
    }

    return bytes_to_write;
}
