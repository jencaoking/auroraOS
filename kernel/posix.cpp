#include "posix.hpp"
#include "vfs.hpp"
#include "task.hpp"
#include "semaphore.hpp"

#include "syscall.hpp"

extern "C" {

int open(const char* path, int flags) {
    // 目前 VfsManager::open 没有处理 flags 参数，暂时忽略
    (void)flags;
    return VfsManager::instance().open(path);
}

int close(int fd) {
    VfsManager::instance().close(fd);
    return 0;
}

int read(int fd, void* buf, size_t count) {
    return VfsManager::instance().read(fd, static_cast<char*>(buf), count);
}

int write(int fd, const void* buf, size_t count) {
    return VfsManager::instance().write(fd, static_cast<const char*>(buf), count);
}

int ioctl(int fd, int request, void* arg) {
    return VfsManager::instance().ioctl(fd, request, arg);
}

int lseek(int fd, int offset, int whence) {
    (void)whence;
    VfsManager::instance().lseek(fd, offset);
    return offset;
}

void sleep(uint32_t seconds) {
    // 假设 1 tick = 1ms，这里转换为 ticks 延时
    sys_sleep(seconds * 1000);
}

void usleep(uint32_t usec) {
    uint32_t ticks = usec / 1000;
    if (ticks == 0) ticks = 1; // 至少休眠 1 个 tick 让出 CPU
    sys_sleep(ticks);
}

int sem_init(sem_t* sem, int pshared, unsigned int value) {
    (void)pshared;
    Semaphore* s = new Semaphore(value);
    *sem = s;
    return 0;
}

int sem_wait(sem_t* sem) {
    Semaphore* s = static_cast<Semaphore*>(*sem);
    if (s) {
        s->wait();
        return 0;
    }
    return -1;
}

int sem_post(sem_t* sem) {
    Semaphore* s = static_cast<Semaphore*>(*sem);
    if (s) {
        s->signal();
        return 0;
    }
    return -1;
}

int sem_destroy(sem_t* sem) {
    Semaphore* s = static_cast<Semaphore*>(*sem);
    if (s) {
        delete s;
        *sem = nullptr;
    }
    return 0;
}

}
