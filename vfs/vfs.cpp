#include "vfs.hpp"
#include "device.hpp"

bool VfsManager::strings_equal(const char* s1, const char* s2) const {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

void VfsManager::str_copy(char* dest, const char* src, int max_len) {
    int i = 0;
    while (*src && i < max_len - 1) dest[i++] = *src++;
    dest[i] = '\0';
}

// 辅助函数：判断 s1 是否是 s2 的前缀，若是，返回前缀长度，否则返回 0
static int starts_with(const char* prefix, const char* str) {
    if (!prefix || !str) return 0;
    int len = 0;
    while (*prefix) {
        if (*prefix != *str) return 0;
        prefix++;
        str++;
        len++;
    }
    return len;
}

void VfsManager::init() {
    LockGuard lock(vfs_mutex_);
    mount_count_ = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table_[i].used = false;
        fd_table_[i].priv = nullptr;
    }
}

bool VfsManager::mount(const char* path, VNode* vnode) {
    LockGuard lock(vfs_mutex_);
    if (mount_count_ >= MAX_MOUNT_POINTS) return false;
    str_copy(mounts_[mount_count_].path, path, sizeof(mounts_[0].path));
    mounts_[mount_count_].vnode = vnode;
    mount_count_++;
    return true;
}

int VfsManager::open(const char* path, int flags) {
    LockGuard lock(vfs_mutex_);
    VNode* target = nullptr;
    int max_prefix_len = 0;
    
    // 最长前缀匹配
    for (int i = 0; i < mount_count_; i++) {
        int prefix_len = starts_with(mounts_[i].path, path);
        if (prefix_len > max_prefix_len) {
            // 确保匹配在目录边界，或者完全匹配
            if (path[prefix_len] == '\0' || path[prefix_len] == '/' || mounts_[i].path[prefix_len - 1] == '/') {
                max_prefix_len = prefix_len;
                target = mounts_[i].vnode;
            }
        }
    }
    if (!target) return -1;

    // 如果路径前缀匹配完，剩余的路径作为相对路径传入 VNode
    const char* relative_path = path + max_prefix_len;
    // 去除前导 '/'
    while (*relative_path == '/') relative_path++;
    
    // 特殊情况：如果相对路径为空但需要文件名，可以传一个表示根的空字符串或 "/"
    if (*relative_path == '\0') relative_path = "/";

    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (!fd_table_[fd].used) {
            fd_table_[fd].used = true; // 先占位
            fd_table_[fd].priv = nullptr;
            
            // 尝试让底层 VNode 处理特定于节点的打开逻辑
            if (target->open_file(relative_path, flags, &fd_table_[fd].priv) < 0) {
                fd_table_[fd].used = false; // 回滚
                return -1; // 底层拒绝打开
            }
            
            fd_table_[fd].vnode = target;
            fd_table_[fd].offset = 0;  // 默认游标在文件开头
            return fd;
        }
    }
    return -1;
}

int VfsManager::read(int fd, char* buf, int len) {
    LockGuard lock(vfs_mutex_);
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table_[fd].used) return -1;
    // 透传 offset 给具体节点，并自动推进游标
    int bytes = fd_table_[fd].vnode->read(buf, len, fd_table_[fd].offset, fd_table_[fd].priv);
    if (bytes > 0) fd_table_[fd].offset += bytes;
    return bytes;
}

int VfsManager::write(int fd, const char* buf, int len) {
    LockGuard lock(vfs_mutex_);
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table_[fd].used) return -1;
    int bytes = fd_table_[fd].vnode->write(buf, len, fd_table_[fd].offset, fd_table_[fd].priv);
    if (bytes > 0) fd_table_[fd].offset += bytes;
    return bytes;
}

int VfsManager::lseek(int fd, int offset, int whence) {
    LockGuard lock(vfs_mutex_);
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        int new_offset = -1;
        if (whence == 0) { // SEEK_SET
            new_offset = offset;
        } else if (whence == 1) { // SEEK_CUR
            new_offset = fd_table_[fd].offset + offset;
        } else if (whence == 2) { // SEEK_END
            new_offset = fd_table_[fd].vnode->get_size(fd_table_[fd].priv) + offset;
        }
        
        if (new_offset >= 0) {
            fd_table_[fd].offset = new_offset;
            return new_offset;
        }
    }
    return -1;
}

void VfsManager::close(int fd) {
    LockGuard lock(vfs_mutex_);
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        fd_table_[fd].vnode->close_file(fd_table_[fd].priv);
        fd_table_[fd].used = false;
        fd_table_[fd].priv = nullptr;
    }
}

int VfsManager::ioctl(int fd, int request, void* arg) {
    LockGuard lock(vfs_mutex_);
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        return fd_table_[fd].vnode->ioctl(request, arg, fd_table_[fd].priv);
    }
    return -1;
}


