#ifndef AURORA_FLASH_DEVICE_HPP
#define AURORA_FLASH_DEVICE_HPP

#include <stdint.h>
#include "device.hpp"
#include "mutex.hpp"

class FlashBlockDevice : public BlockDevice {
private:
    uint8_t* memory_;
    uint32_t block_size_;  // 扇区擦除大小：4KB (4096 Bytes)
    uint32_t block_count_; // 扇区总数：128 个 (总容量 512KB)
    uint32_t page_size_;   // 物理页编程大小：256 Bytes
    Mutex    hw_mutex_;

public:
    FlashBlockDevice(const char* name, uint32_t block_size = 4096, uint32_t block_count = 128, uint32_t page_size = 256)
        : BlockDevice(name), block_size_(block_size), block_count_(block_count), page_size_(page_size) {
        memory_ = new uint8_t[block_size_ * block_count_];
        // 物理闪存出厂默认全为 0xFF
        for (uint32_t i = 0; i < block_size_ * block_count_; i++) {
            memory_[i] = 0xFF;
        }
    }

    ~FlashBlockDevice() {
        delete[] memory_;
    }

    int open() override { return 0; }
    int close() override { return 0; }

    // ========================================================
    // 闪存读取：直接读取指定物理块与偏移处的数据
    // ========================================================
    int read_blocks(uint32_t block_addr, uint32_t offset, uint8_t* buf, uint32_t size) {
        if (block_addr >= block_count_ || offset + size > block_size_) return -1;
        
        hw_mutex_.lock();
        uint32_t phys_addr = (block_addr * block_size_) + offset;
        for (uint32_t i = 0; i < size; i++) {
            buf[i] = memory_[phys_addr + i];
        }
        hw_mutex_.unlock();
        return 0;
    }

    // ========================================================
    // 闪存写入：严格模拟物理 NOR Flash 的按位与 (AND) 写入特性
    // ========================================================
    int write_blocks(uint32_t block_addr, uint32_t offset, const uint8_t* buf, uint32_t size) {
        if (block_addr >= block_count_ || offset + size > block_size_) return -1;
        
        hw_mutex_.lock();
        uint32_t phys_addr = (block_addr * block_size_) + offset;
        for (uint32_t i = 0; i < size; i++) {
            // 物理闪存不擦除无法把 0 变成 1，只能通过位与 (&=) 将 1 变成 0
            memory_[phys_addr + i] &= buf[i];
        }
        hw_mutex_.unlock();
        return 0;
    }

    // ========================================================
    // 闪存扇区擦除：将整块 4KB 区域重置为 0xFF
    // ========================================================
    int erase_block(uint32_t block_addr) {
        if (block_addr >= block_count_) return -1;
        
        hw_mutex_.lock();
        uint32_t phys_addr = block_addr * block_size_;
        for (uint32_t i = 0; i < block_size_; i++) {
            memory_[phys_addr + i] = 0xFF;
        }
        hw_mutex_.unlock();
        return 0;
    }

    uint32_t get_block_size() const { return block_size_; }
    uint32_t get_block_count() const { return block_count_; }
    uint32_t get_page_size() const { return page_size_; }
};

#endif
