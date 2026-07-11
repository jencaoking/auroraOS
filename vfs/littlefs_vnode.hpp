#ifndef AURORA_LITTLEFS_VNODE_HPP
#define AURORA_LITTLEFS_VNODE_HPP

#include "vfs.hpp"
#include "photon_cache.hpp"
// 引入第三方开源库接口 (假定项目附带于 3rdparty/littlefs/lfs.h)
extern "C" {
#include "lfs.h"
}

class LittleFsAdapter {
private:
    lfs_t        lfs_;
    lfs_config   cfg_;
    PhotonCacheLayer& cache_;
    bool         is_mounted_;

    // ========================================================
    // 绑定给 LittleFS 的静态底层操作桥接回调
    // ========================================================
    static int lfs_read_bridge(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
        auto* cache = static_cast<PhotonCacheLayer*>(c->context);
        return cache->read(block, off, static_cast<uint8_t*>(buffer), size);
    }

    static int lfs_prog_bridge(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
        auto* cache = static_cast<PhotonCacheLayer*>(c->context);
        return cache->write(block, off, static_cast<const uint8_t*>(buffer), size);
    }

    static int lfs_erase_bridge(const struct lfs_config *c, lfs_block_t block) {
        auto* cache = static_cast<PhotonCacheLayer*>(c->context);
        return cache->erase(block);
    }

    static int lfs_sync_bridge(const struct lfs_config *c) {
        auto* cache = static_cast<PhotonCacheLayer*>(c->context);
        return cache->sync();
    }

public:
    LittleFsAdapter(PhotonCacheLayer& cache, uint32_t block_size = 4096, uint32_t block_count = 128) 
        : cache_(cache), is_mounted_(false) {
        
        cfg_.context     = &cache_;
        cfg_.read        = lfs_read_bridge;
        cfg_.prog        = lfs_prog_bridge;
        cfg_.erase       = lfs_erase_bridge;
        cfg_.sync        = lfs_sync_bridge;
        
        cfg_.read_size   = 256;
        cfg_.prog_size   = 256;
        cfg_.block_size  = block_size;
        cfg_.block_count = block_count;
        cfg_.cache_size  = 256;
        cfg_.lookahead_size = 16;
        cfg_.block_cycles = 500; // 开启动态磨损平衡
        
        // Zero out remaining fields (e.g. name_max, file_max, attr_max, etc)
        // LittleFS expects these to be set or 0 to use defaults.
        cfg_.name_max = 0;
        cfg_.file_max = 0;
        cfg_.attr_max = 0;
        cfg_.metadata_max = 0;
    }

    bool mount() {
        int err = lfs_mount(&lfs_, &cfg_);
        if (err != 0) {
            // 如果初次挂载失败（全新出厂芯片），则立刻格式化并重新挂载
            lfs_format(&lfs_, &cfg_);
            err = lfs_mount(&lfs_, &cfg_);
        }
        is_mounted_ = (err == 0);
        return is_mounted_;
    }

    lfs_t* get_lfs() { return &lfs_; }
};

// 继承自 VNode 的具体具体日志文件实现
class LfsFileNode : public VNode {
private:
    LittleFsAdapter& fs_;
    lfs_file_t       file_;
    bool             is_open_;

public:
    LfsFileNode(LittleFsAdapter& fs) : fs_(fs), is_open_(false) {}

    int open_file(const char* path, int flags) {
        int lfs_flags = LFS_O_RDWR | LFS_O_CREAT;
        int err = lfs_file_open(fs_.get_lfs(), &file_, path, lfs_flags);
        is_open_ = (err == 0);
        return is_open_ ? 0 : -1;
    }

    int read(char* buf, int len, int offset) override {
        if (!is_open_) return -1;
        lfs_file_seek(fs_.get_lfs(), &file_, offset, LFS_SEEK_SET);
        return lfs_file_read(fs_.get_lfs(), &file_, buf, len);
    }

    int write(const char* buf, int len, int offset) override {
        if (!is_open_) return -1;
        lfs_file_seek(fs_.get_lfs(), &file_, offset, LFS_SEEK_SET);
        int bytes = lfs_file_write(fs_.get_lfs(), &file_, buf, len);
        // 核心保护：写入完成自动触发 LittleFS 元数据树与底层光子页落盘
        lfs_file_sync(fs_.get_lfs(), &file_);
        return bytes;
    }

    void close_file() {
        if (is_open_) {
            lfs_file_close(fs_.get_lfs(), &file_);
            is_open_ = false;
        }
    }
};

#endif
