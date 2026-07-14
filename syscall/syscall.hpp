#ifndef SYSCALL_HPP
#define SYSCALL_HPP

#include <stdint.h>

constexpr uint8_t SYS_PRINT = 0x01;
constexpr uint8_t SYS_YIELD = 0x02;
constexpr uint8_t SYS_SLEEP = 0x03;

// 微内核 IPC 通信接口
constexpr uint8_t SYS_IPC_CALL    = 0x04;
constexpr uint8_t SYS_IPC_RECEIVE = 0x05;
constexpr uint8_t SYS_IPC_REPLY   = 0x06;

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

// ----------------------------------------------------
// IPC: 发送并阻塞等待回复 (同步机制)
// r0 = cap_id (Endpoint 权能槽)
// r1 = msg (发送缓冲区)
// r2 = len (发送长度)
// r3 = reply_buf (接收回复的缓冲区)
// (返回接收到的长度暂略，通过共享内存/缓冲区字段反馈)
inline void sys_ipc_call(uint32_t cap_id, void* msg, uint32_t len, void* reply_buf, uint32_t max_reply_len) {
    __asm__ volatile (
        "mov r0, %0\n\t"
        "mov r1, %1\n\t"
        "mov r2, %2\n\t"
        "mov r3, %3\n\t"
        "push {%4}\n\t"    // 第5个参数压栈或使用 r4 (由于 GCC inline asm 限制，在 Cortex-M 上可以直接放栈)
        "pop {r4}\n\t"     // 这里简单演示，用 r4 传 max_reply_len
        "svc %5\n\t"
        : 
        : "r"(cap_id), "r"(msg), "r"(len), "r"(reply_buf), "r"(max_reply_len), "i"(SYS_IPC_CALL)
        : "r0", "r1", "r2", "r3", "r4"
    );
}

// IPC: 接收请求 (阻塞)
// 返回: 对方传递的数据长度，并且 sender_id 会被填充
inline void sys_ipc_receive(uint32_t cap_id, void* msg_buf, uint32_t max_len, uint32_t* out_sender_id) {
    __asm__ volatile (
        "mov r0, %0\n\t"
        "mov r1, %1\n\t"
        "mov r2, %2\n\t"
        "mov r3, %3\n\t"
        "svc %4\n\t"
        : 
        : "r"(cap_id), "r"(msg_buf), "r"(max_len), "r"(out_sender_id), "i"(SYS_IPC_RECEIVE)
        : "r0", "r1", "r2", "r3"
    );
}

// IPC: 回复请求 (非阻塞，对方恢复执行)
inline void sys_ipc_reply(uint32_t sender_id, void* reply_msg, uint32_t len) {
    __asm__ volatile (
        "mov r0, %0\n\t"
        "mov r1, %1\n\t"
        "mov r2, %2\n\t"
        "svc %3\n\t"
        : 
        : "r"(sender_id), "r"(reply_msg), "r"(len), "i"(SYS_IPC_REPLY)
        : "r0", "r1", "r2"
    );
}

#endif
