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

void VfsManager::init() {
    mount_count_ = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table_[i].used = false;
    }
}

bool VfsManager::mount(const char* path, VNode* vnode) {
    if (mount_count_ >= MAX_MOUNT_POINTS) return false;
    mounts_[mount_count_].path = path;
    mounts_[mount_count_].vnode = vnode;
    mount_count_++;
    return true;
}

int VfsManager::open(const char* path) {
    VNode* target = nullptr;
    // TODO(L6): Currently this only supports exact path matching (e.g. "/tmp/log.txt").
    // A real VFS should implement hierarchical path resolution:
    // matching the longest mount prefix (e.g. "/tmp") and passing the remainder ("log.txt") to the VNode.
    for (int i = 0; i < mount_count_; i++) {
        if (strings_equal(mounts_[i].path, path)) {
            target = mounts_[i].vnode; break;
        }
    }
    if (!target) return -1;

    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (!fd_table_[fd].used) {
            fd_table_[fd].vnode = target;
            fd_table_[fd].offset = 0;  // 默认游标在文件开头
            fd_table_[fd].used = true;
            return fd;
        }
    }
    return -1;
}

int VfsManager::read(int fd, char* buf, int len) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table_[fd].used) return -1;
    // 透传 offset 给具体节点，并自动推进游标
    int bytes = fd_table_[fd].vnode->read(buf, len, fd_table_[fd].offset);
    if (bytes > 0) fd_table_[fd].offset += bytes;
    return bytes;
}

int VfsManager::write(int fd, const char* buf, int len) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table_[fd].used) return -1;
    int bytes = fd_table_[fd].vnode->write(buf, len, fd_table_[fd].offset);
    if (bytes > 0) fd_table_[fd].offset += bytes;
    return bytes;
}

int VfsManager::lseek(int fd, int offset, int whence) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        int new_offset = -1;
        if (whence == 0) { // SEEK_SET
            new_offset = offset;
        } else if (whence == 1) { // SEEK_CUR
            new_offset = fd_table_[fd].offset + offset;
        } else if (whence == 2) { // SEEK_END
            new_offset = fd_table_[fd].vnode->get_size() + offset;
        }
        
        if (new_offset >= 0) {
            fd_table_[fd].offset = new_offset;
            return new_offset;
        }
    }
    return -1;
}

void VfsManager::close(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES) fd_table_[fd].used = false;
}

int VfsManager::ioctl(int fd, int request, void* arg) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        return fd_table_[fd].vnode->ioctl(request, arg);
    }
    return -1;
}


