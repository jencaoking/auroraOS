#ifndef AURORA_POSIX_HPP
#define AURORA_POSIX_HPP

#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

// 线程本地 errno 宏
int* __errno_location();
#undef errno
#define errno (*__errno_location())


#ifdef __cplusplus
} // 关闭上面的 extern "C"
#endif

#ifdef AURORA_HOST_TEST
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 标准 POSIX 文件打开标志
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0200
#define O_TRUNC     0x0400
#define O_APPEND    0x0800

#ifndef AURORA_HOST_TEST
// 标准文件操作接口
int open(const char* path, int flags);
int close(int fd);
int read(int fd, void* buf, size_t count);
int write(int fd, const void* buf, size_t count);
int ioctl(int fd, int request, void* arg);
int lseek(int fd, int offset, int whence);

// 延时接口
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
#endif

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
