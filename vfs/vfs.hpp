#ifndef VFS_HPP
#define VFS_HPP

#include "mutex.hpp"

class VNode {
public:
    virtual ~VNode() = default;
    // 增加 open_file, 传递 flags 和 priv 指针
    virtual int open_file(const char* /*path*/, int /*flags*/, void** /*priv*/) { return 0; }
    virtual void close_file(void* /*priv*/) {}
    // 增加 offset 参数
    virtual int read(char* /*buf*/, int /*len*/, int /*offset*/, void* /*priv*/) { return -1; }
    virtual int write(const char* /*buf*/, int /*len*/, int /*offset*/, void* /*priv*/) { return -1; }
    virtual int ioctl(int /*request*/, void* /*arg*/, void* /*priv*/) { return -1; }
    virtual int get_size(void* /*priv*/) const { return 0; }
};

struct MountPoint {
    char path[32];
    VNode* vnode;
};

// 真正的文件描述符：记录打开的文件以及当前的读写位置
struct FileDescriptor {
    VNode* vnode;
    int offset;
    bool used;
    void* priv;
};

class VfsManager {
public:
    static VfsManager& instance() {
        static VfsManager vfs;
        return vfs;
    }

    void init();
    bool mount(const char* path, VNode* vnode);
    int open(const char* path, int flags = 0);
    int read(int fd, char* buf, int len);
    int write(int fd, const char* buf, int len);
    int close(int fd);
    int ioctl(int fd, int request, void* arg);
    
    // 【新增】系统调用：移动文件读写游标
    int lseek(int fd, int offset, int whence);

private:
    VfsManager() = default;
    VfsManager(const VfsManager&) = delete;
    VfsManager& operator=(const VfsManager&) = delete;

    static constexpr int MAX_MOUNT_POINTS = 8;
    static constexpr int MAX_OPEN_FILES = 16;

    MountPoint mounts_[MAX_MOUNT_POINTS]{};
    int mount_count_ = 0;

    // 升级为完整的文件描述符表
    FileDescriptor fd_table_[MAX_OPEN_FILES]{};
    Mutex vfs_mutex_;

    bool strings_equal(const char* s1, const char* s2) const;
    void str_copy(char* dest, const char* src, int max_len);
};

#endif
