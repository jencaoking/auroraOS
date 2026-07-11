#include "posix.hpp"
#include "vfs.hpp"
#include "task.hpp"
#include "semaphore.hpp"

#include "syscall.hpp"

extern "C" {

int open(const char* path, int flags) {
    (void)flags;
#ifdef CONFIG_VFS
    return VfsManager::instance().open(path);
#else
    return -1;
#endif
}

int close(int fd) {
#ifdef CONFIG_VFS
    VfsManager::instance().close(fd);
    return 0;
#else
    (void)fd;
    return -1;
#endif
}

int read(int fd, void* buf, size_t count) {
#ifdef CONFIG_VFS
    return VfsManager::instance().read(fd, static_cast<char*>(buf), count);
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int write(int fd, const void* buf, size_t count) {
#ifdef CONFIG_VFS
    return VfsManager::instance().write(fd, static_cast<const char*>(buf), count);
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int ioctl(int fd, int request, void* arg) {
#ifdef CONFIG_VFS
    return VfsManager::instance().ioctl(fd, request, arg);
#else
    (void)fd; (void)request; (void)arg;
    return -1;
#endif
}

int lseek(int fd, int offset, int whence) {
    (void)whence;
#ifdef CONFIG_VFS
    VfsManager::instance().lseek(fd, offset);
    return offset;
#else
    (void)fd; (void)offset;
    return -1;
#endif
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
