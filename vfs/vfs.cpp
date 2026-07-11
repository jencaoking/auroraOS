#include "vfs.hpp"
#include "device.hpp"

bool VfsManager::strings_equal(const char* s1, const char* s2) {
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

void VfsManager::lseek(int fd, int offset) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && fd_table_[fd].used) {
        fd_table_[fd].offset = offset;
    }
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

#include "uart.h"
#include "mutex.hpp"
#include "task.hpp"
#include "syscall.hpp"

extern Mutex uart_mutex;

class UartDevice : public CharDevice {
public:
    UartDevice() : CharDevice("uart0") {}
    
    int write(const char* buf, int len, int offset) override {
        (void)offset; // UART is a stream device, ignore offset
        LockGuard lock(uart_mutex);
        for (int i = 0; i < len; i++) {
            uart_putc(buf[i]);
        }
        return len;
    }

    int read(char* buf, int len, int offset) override {
        int bytes_read = 0;
        // 保留 1 个字节给末尾的 '\0' 字符串截断符
        while (bytes_read < len - 1) { 
            char c;
            if (uart_getc_nb(&c)) {
                if (c == '\r' || c == '\n') {
                    // 按下回车，结束当前行的读取
                    buf[bytes_read] = '\0';
                    sys_print("\r\n");
                    break;
                } else if (c == '\b' || c == 127) { 
                    // 处理退格键：不仅要删掉缓冲区的数据，还要在终端界面上抹掉字符
                    if (bytes_read > 0) {
                        bytes_read--;
                        sys_print("\b \b");
                    }
                } else {
                    // 正常字符，存入缓冲区并立即在终端回显
                    buf[bytes_read++] = c;
                    char temp[2] = {c, '\0'};
                    sys_print(temp); 
                }
            } else {
                // 如果当前没有敲击键盘，立刻让出 CPU，通过 Syscall 休眠 5ms
                sys_sleep(5); 
            }
        }
        return bytes_read;
    }
};

Device* create_uart_device() {
    return new UartDevice();
}
