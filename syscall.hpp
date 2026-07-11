#ifndef SYSCALL_HPP
#define SYSCALL_HPP

#include <stdint.h>

// 定义用户态接口
inline void sys_print(const char* str) {
    // 触发 SVC 0x01，参数 str 放在 r0 寄存器中
    __asm__ volatile (
        "mov r0, %0\n\t"
        "svc #0x01\n\t"
        : 
        : "r"(str) 
        : "r0"
    );
}

inline void sys_yield() {
    __asm__ volatile ("svc #0x02" : : : );
}

inline void sys_sleep(uint32_t ticks) {
    // 触发 SVC 0x03，参数 ticks 放在 r0 寄存器中
    __asm__ volatile (
        "mov r0, %0\n\t"
        "svc #0x03\n\t"
        : 
        : "r"(ticks) 
        : "r0"
    );
}

#endif
