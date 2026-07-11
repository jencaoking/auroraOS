#ifndef TASK_HPP
#define TASK_HPP

#include <stdint.h>

// 线程状态
enum class TaskState {
    Ready,
    Running
};

// 线程控制块 (TCB)
struct TaskControlBlock {
    uint32_t* stack_ptr;       // 必须是第一个成员！供汇编直接通过偏移量 0 读取
    TaskState state;
    uint32_t  id;
};

class Scheduler {
public:
    static Scheduler& instance() {
        static Scheduler sched;
        return sched;
    }

    // 初始化就绪队列
    void init() {
        current_task_index = 0;
        task_count = 0;
    }

    // 创建新线程
    void create_task(void (*task_entry)(void), uint32_t* stack_space, uint32_t stack_size) {
        if (task_count >= MAX_TASKS) return;

        TaskControlBlock& tcb = tasks[task_count];
        tcb.id = task_count;
        tcb.state = TaskState::Ready;

        // 模拟硬件压栈：在独立的栈空间尾部伪造一个初始的中断帧 (Interrupt Frame)
        uint32_t* top = stack_space + (stack_size / sizeof(uint32_t));
        
        top--; *top = 0x01000000;   // xPSR: 必须设置 Thumb 状态位 (Bit 24)
        top--; *top = reinterpret_cast<uint32_t>(task_entry); // PC: 线程入口函数地址
        top--; *top = 0xFFFFFFFD;   // LR: 退出时返回 0xFFFFFFFD (表示返回后使用进程堆栈 PSP)
        top--; *top = 0;            // R12
        top--; *top = 0;            // R3
        top--; *top = 0;            // R2
        top--; *top = 0;            // R1
        top--; *top = 0;            // R0

        // 模拟软件压栈：手动放置 r4 - r11 的初始值（全设为 0 即可）
        for (int i = 0; i < 8; i++) {
            top--;
            *top = 0;
        }

        tcb.stack_ptr = top; // 将伪造好上下文的栈顶存入 TCB
        task_count++;
    }

    // 选出下一个要运行的线程（时间片轮转切换）
    void schedule() {
        if (task_count <= 1) return;

        current_task_index = (current_task_index + 1) % task_count;

        // 触发 PendSV 中断，通知 CPU 执行上下文切换
        // 向 ICSR 寄存器 (0xE000ED04) 的 Bit 28 写 1 挂起 PendSV
        *reinterpret_cast<volatile uint32_t*>(0xE000ED04) = (1 << 28);
    }

    TaskControlBlock* get_current_tcb() { return &tasks[current_task_index]; }
    TaskControlBlock* get_next_tcb() { return &tasks[current_task_index]; } // 触发中断后当前 index 已变

private:
    Scheduler() = default;
    static constexpr int MAX_TASKS = 4;
    TaskControlBlock tasks[MAX_TASKS];
    uint32_t current_task_index = 0;
    uint32_t task_count = 0;
};

// 供汇编直接呼叫的两个全局指针，用来读取旧 SP 和写入新 SP
extern "C" {
    extern TaskControlBlock* g_current_tcb_ptr;
    extern TaskControlBlock* g_next_tcb_ptr;
}

#endif