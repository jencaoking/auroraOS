#ifndef TASK_HPP
#define TASK_HPP

#include <stdint.h>

// 1. 增加 Sleeping 状态
enum class TaskState {
    Ready,
    Running,
    Sleeping 
};

struct TaskControlBlock {
    uint32_t* stack_ptr;       
    TaskState state;
    uint32_t  id;
    uint32_t  sleep_ticks;     // 记录还需要休眠多少个系统 Tick
    uint32_t  control_reg;     // 【新增】保存任务的 CONTROL 寄存器状态
};

extern "C" {
    extern TaskControlBlock* g_current_tcb_ptr;
    extern TaskControlBlock* g_next_tcb_ptr;
}

class Scheduler {
public:
    static Scheduler& instance() {
        static Scheduler sched;
        return sched;
    }

    void init() {
        current_task_index = 0;
        task_count = 0;
    }

    void create_task(void (*task_entry)(void), uint32_t* stack_space, uint32_t stack_size, bool is_privileged = true) {
        if (task_count >= MAX_TASKS) return;

        TaskControlBlock& tcb = tasks[task_count];
        tcb.id = task_count;
        tcb.state = TaskState::Ready;
        tcb.sleep_ticks = 0;
        // CONTROL: Bit 1 (SPSEL) = 1 (Use PSP). Bit 0 (nPRIV) = 0 (Privileged) or 1 (Unprivileged)
        tcb.control_reg = is_privileged ? 0x02 : 0x03;

        uint32_t* top = stack_space + (stack_size / sizeof(uint32_t));
        top--; *top = 0x01000000;   
        top--; *top = reinterpret_cast<uint32_t>(task_entry); 
        top--; *top = 0xFFFFFFFD;   
        top -= 5;                   
        top -= 8;                   

        tcb.stack_ptr = top; 
        task_count++;
    }

    // 2. 调度算法升级：跳过正在休眠的任务
    void schedule() {
        if (task_count <= 1) return;

        uint32_t next_task = current_task_index;
        bool found = false;

        // 轮询寻找下一个处于 Ready 或 Running 状态的任务
        for (uint32_t i = 0; i < task_count; i++) {
            next_task = (next_task + 1) % task_count;
            if (tasks[next_task].state != TaskState::Sleeping) {
                found = true;
                break;
            }
        }

        if (found && next_task != current_task_index) {
            // 在触发中断前，必须先把指针更新好，否则 Thread 模式下会立即陷入 PendSV 导致空指针
            g_current_tcb_ptr = &tasks[current_task_index];
            current_task_index = next_task;
            g_next_tcb_ptr = &tasks[current_task_index];
            
            // 触发 PendSV 切换上下文
            *reinterpret_cast<volatile uint32_t*>(0xE000ED04) = (1 << 28);
        }
    }

    // 3. 让当前线程交出 CPU 并休眠指定时间
    void sleep(uint32_t ticks) {
        TaskControlBlock* current = get_current_tcb();
        current->sleep_ticks = ticks;
        current->state = TaskState::Sleeping;
        schedule(); // 立即触发调度，让出 CPU
    }

    // 4. 供定时器中断调用的时间刷新函数
    void tick_update() {
        for (uint32_t i = 0; i < task_count; i++) {
            if (tasks[i].state == TaskState::Sleeping) {
                if (tasks[i].sleep_ticks > 0) {
                    tasks[i].sleep_ticks--;
                }
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = TaskState::Ready; // 睡醒了，恢复就绪
                }
            }
        }
    }

    TaskControlBlock* get_current_tcb() { return &tasks[current_task_index]; }

private:
    Scheduler() = default;
    static constexpr int MAX_TASKS = 4;
    TaskControlBlock tasks[MAX_TASKS];
    uint32_t current_task_index = 0;
    uint32_t task_count = 0;
};

#endif