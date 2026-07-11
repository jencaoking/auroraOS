#ifndef AURORA_POSIX_HPP
#define AURORA_POSIX_HPP

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 标准文件操作接口
int open(const char* path, int flags);
int close(int fd);
int read(int fd, void* buf, size_t count);
int write(int fd, const void* buf, size_t count);
int ioctl(int fd, int request, void* arg);
int lseek(int fd, int offset, int whence);

// 延时接口
void sleep(uint32_t seconds);
void usleep(uint32_t usec);

// POSIX 信号量不透明指针
typedef void* sem_t;

// 信号量接口
int sem_init(sem_t* sem, int pshared, unsigned int value);
int sem_wait(sem_t* sem);
int sem_post(sem_t* sem);
int sem_destroy(sem_t* sem);

#ifdef __cplusplus
}
#endif

#endif
