#ifndef VFS_HPP
#define VFS_HPP

class VNode {
public:
    virtual ~VNode() = default;
    // 增加 offset 参数
    virtual int read(char* buf, int len, int offset) { return -1; }
    virtual int write(const char* buf, int len, int offset) { return -1; }
    virtual int ioctl(int request, void* arg) { return -1; }
};

struct MountPoint {
    const char* path;
    VNode* vnode;
};

// 真正的文件描述符：记录打开的文件以及当前的读写位置
struct FileDescriptor {
    VNode* vnode;
    int offset;
    bool used;
};

class VfsManager {
public:
    static VfsManager& instance() {
        static VfsManager vfs;
        return vfs;
    }

    void init();
    bool mount(const char* path, VNode* vnode);
    int open(const char* path);
    int read(int fd, char* buf, int len);
    int write(int fd, const char* buf, int len);
    void close(int fd);
    int ioctl(int fd, int request, void* arg);
    
    // 【新增】系统调用：移动文件读写游标
    void lseek(int fd, int offset);

private:
    VfsManager() = default;
    static constexpr int MAX_MOUNT_POINTS = 8;
    static constexpr int MAX_OPEN_FILES = 16;

    MountPoint mounts_[MAX_MOUNT_POINTS];
    int mount_count_ = 0;

    // 升级为完整的文件描述符表
    FileDescriptor fd_table_[MAX_OPEN_FILES];

    bool strings_equal(const char* s1, const char* s2);
};

#endif
