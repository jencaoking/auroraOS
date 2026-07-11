#ifndef SYSCALL_HPP
#define SYSCALL_HPP

#include <stdint.h>

constexpr uint8_t SYS_PRINT = 0x01;
constexpr uint8_t SYS_YIELD = 0x02;
constexpr uint8_t SYS_SLEEP = 0x03;

// 定义用户态接口
inline void sys_print(const char* str) {
    // 触发 SVC SYS_PRINT，参数 str 放在 r0 寄存器中
    __asm__ volatile (
        "mov r0, %0\n\t"
        "svc %1\n\t"
        : 
        : "r"(str), "i"(SYS_PRINT)
        : "r0"
    );
}

inline void sys_yield() {
    __asm__ volatile (
        "svc %0"
        :
        : "i"(SYS_YIELD)
    );
}

inline void sys_sleep(uint32_t ticks) {
    // 触发 SVC SYS_SLEEP，参数 ticks 放在 r0 寄存器中
    __asm__ volatile (
        "mov r0, %0\n\t"
        "svc %1\n\t"
        : 
        : "r"(ticks), "i"(SYS_SLEEP)
        : "r0"
    );
}

#endif
