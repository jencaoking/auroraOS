#ifndef AURORA_PHOTON_CACHE_HPP
#define AURORA_PHOTON_CACHE_HPP

#include <stdint.h>
#include "../drivers/storage/flash_device.hpp"
#include "mutex.hpp"

struct CachePage {
    uint32_t block_addr;
    uint32_t page_offset;
    uint8_t  data[256]; // 对齐闪存 256B 物理页
    bool     is_valid;
    bool     is_dirty;
    uint32_t last_access_tick;
};

class PhotonCacheLayer {
private:
    static constexpr int CACHE_POOL_SIZE = 8; // 在 RAM 中开辟 8 个页缓存槽位 (共 2KB)
    CachePage        pool_[CACHE_POOL_SIZE];
    FlashBlockDevice& flash_;
    Mutex            cache_mutex_;
    uint32_t         tick_counter_;

    // 内部方法：将脏页真正刷入底层闪存
    int flush_page(int index) {
        if (!pool_[index].is_valid || !pool_[index].is_dirty) return 0;
        
        int res = flash_.write_blocks(
            pool_[index].block_addr, 
            pool_[index].page_offset, 
            pool_[index].data, 
            flash_.get_page_size()
        );
        
        if (res == 0) pool_[index].is_dirty = false;
        return res;
    }

    // 内部方法：LRU (最近最少使用) 算法淘汰替换缓存槽位
    int get_or_alloc_page(uint32_t block_addr, uint32_t page_offset, bool for_write) {
        int oldest_idx = 0;
        uint32_t oldest_tick = 0xFFFFFFFF;

        // 1. 命中检测
        for (int i = 0; i < CACHE_POOL_SIZE; i++) {
            if (pool_[i].is_valid && pool_[i].block_addr == block_addr && pool_[i].page_offset == page_offset) {
                pool_[i].last_access_tick = ++tick_counter_;
                return i;
            }
            if (!pool_[i].is_valid) {
                oldest_idx = i;
                oldest_tick = 0;
            } else if (pool_[i].last_access_tick < oldest_tick) {
                oldest_idx = i;
                oldest_tick = pool_[i].last_access_tick;
            }
        }

        // 2. 缓存未命中，如果被淘汰的页是脏页，必须先将其落盘保护
        if (pool_[oldest_idx].is_valid && pool_[oldest_idx].is_dirty) {
            int res = flush_page(oldest_idx);
            if (res != 0) {
                return -1; // 刷盘失败，拒绝覆盖该脏页，向上层报错
            }
        }

        // 3. 载入新物理页到该缓存槽位
        pool_[oldest_idx].block_addr = block_addr;
        pool_[oldest_idx].page_offset = page_offset;
        pool_[oldest_idx].is_valid = true;
        pool_[oldest_idx].is_dirty = false;
        pool_[oldest_idx].last_access_tick = ++tick_counter_;

        if (!for_write) {
            flash_.read_blocks(block_addr, page_offset, pool_[oldest_idx].data, flash_.get_page_size());
        } else {
            // 如果是整页写覆盖，先读入旧背景数据，防止修改页内局部字节时丢失其他数据
            flash_.read_blocks(block_addr, page_offset, pool_[oldest_idx].data, flash_.get_page_size());
        }

        return oldest_idx;
    }

public:
    PhotonCacheLayer(FlashBlockDevice& flash) : flash_(flash), tick_counter_(0) {
        for (int i = 0; i < CACHE_POOL_SIZE; i++) {
            pool_[i].is_valid = false;
            pool_[i].is_dirty = false;
        }
    }

    ~PhotonCacheLayer() { sync(); }

    // ========================================================
    // 光子缓存读取：0 闪存 I/O 极速 RAM 命中返回
    // ========================================================
    int read(uint32_t block_addr, uint32_t offset, uint8_t* buf, uint32_t size) {
        cache_mutex_.lock();
        uint32_t page_size = flash_.get_page_size();
        uint32_t bytes_read = 0;

        while (bytes_read < size) {
            uint32_t curr_offset = offset + bytes_read;
            uint32_t page_offset = curr_offset & ~(page_size - 1);
            uint32_t in_page_idx = curr_offset & (page_size - 1);
            uint32_t chunk = page_size - in_page_idx;
            if (chunk > size - bytes_read) chunk = size - bytes_read;

            int cache_idx = get_or_alloc_page(block_addr, page_offset, false);
            for (uint32_t i = 0; i < chunk; i++) {
                buf[bytes_read + i] = pool_[cache_idx].data[in_page_idx + i];
            }
            bytes_read += chunk;
        }
        cache_mutex_.unlock();
        return 0;
    }

    // ========================================================
    // 光子缓存写入：将小写聚拢在 RAM 缓冲页，绝不立刻触发闪存磨损
    // ========================================================
    int write(uint32_t block_addr, uint32_t offset, const uint8_t* buf, uint32_t size) {
        cache_mutex_.lock();
        uint32_t page_size = flash_.get_page_size();
        uint32_t bytes_written = 0;

        while (bytes_written < size) {
            uint32_t curr_offset = offset + bytes_written;
            uint32_t page_offset = curr_offset & ~(page_size - 1);
            uint32_t in_page_idx = curr_offset & (page_size - 1);
            uint32_t chunk = page_size - in_page_idx;
            if (chunk > size - bytes_written) chunk = size - bytes_written;

            int cache_idx = get_or_alloc_page(block_addr, page_offset, true);
            for (uint32_t i = 0; i < chunk; i++) {
                pool_[cache_idx].data[in_page_idx + i] = buf[bytes_written + i];
            }
            pool_[cache_idx].is_dirty = true; // 仅标记为脏，延迟物理落盘！
            bytes_written += chunk;
        }
        cache_mutex_.unlock();
        return 0;
    }

    int erase(uint32_t block_addr) {
        cache_mutex_.lock();
        // 如果要擦除某块，先让缓存池中属于该块的所有页失效
        for (int i = 0; i < CACHE_POOL_SIZE; i++) {
            if (pool_[i].is_valid && pool_[i].block_addr == block_addr) {
                pool_[i].is_valid = false;
                pool_[i].is_dirty = false;
            }
        }
        int res = flash_.erase_block(block_addr);
        cache_mutex_.unlock();
        return res;
    }

    // ========================================================
    // 显式同步 (Sync)：将所有脏页批量高效刷入闪存
    // ========================================================
    int sync() {
        int final_res = 0;
        cache_mutex_.lock();
        for (int i = 0; i < CACHE_POOL_SIZE; i++) {
            if (pool_[i].is_valid && pool_[i].is_dirty) {
                int res = flush_page(i);
                if (res != 0 && final_res == 0) {
                    final_res = res; // 记录第一次遇到的错误
                }
            }
        }
        cache_mutex_.unlock();
        return final_res;
    }
};

#endif
