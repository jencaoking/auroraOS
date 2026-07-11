#include "vfs.hpp"

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

#include "uart.h"
#include "mutex.hpp"
#include "task.hpp"

extern Mutex uart_mutex;

class UartDevice : public VNode {
public:
    int write(const char* buf, int len, int offset) override {
        uart_mutex.lock();
        for (int i = 0; i < len; i++) uart_putc(buf[i]);
        uart_mutex.unlock();
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
                    uart_putc('\r'); uart_putc('\n');
                    break;
                } else if (c == '\b' || c == 127) { 
                    // 处理退格键：不仅要删掉缓冲区的数据，还要在终端界面上抹掉字符
                    if (bytes_read > 0) {
                        bytes_read--;
                        uart_putc('\b'); uart_putc(' '); uart_putc('\b');
                    }
                } else {
                    // 正常字符，存入缓冲区并立即在终端回显
                    buf[bytes_read++] = c;
                    uart_putc(c); 
                }
            } else {
                // 如果当前没有敲击键盘，立刻让出 CPU，休眠 5ms
                Scheduler::instance().sleep(5); 
            }
        }
        return bytes_read;
    }
};

UartDevice g_uart_device;
