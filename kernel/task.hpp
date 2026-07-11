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
    extern volatile TaskControlBlock* g_current_tcb_ptr;
    extern volatile TaskControlBlock* g_next_tcb_ptr;
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
        
        // ====================================================================================
        // 【架构安全声明：名义特权分离 vs 绝对隔离】
        // ------------------------------------------------------------------------------------
        // 这里的 is_privileged 参数通过设置 CONTROL 寄存器的 nPRIV 位（Bit 0）将线程降权为用户态。
        // 用户态下将无法访问系统控制空间（SCS，如 NVIC/SCB），也无法执行 cpsid 等特权指令。
        //
        // ⚠️ 局限性警告 (Limitation)：
        // 目前系统尚未配置 MPU (Memory Protection Unit)。在 Cortex-M 的默认内存映射中，
        // SRAM 区域（内核数据、堆、栈所在的 0x20000000~ 区域）对非特权模式依然是全尺寸读写的！
        // 因此，目前用户态代码理论上仍可以越权篡改 Scheduler::tasks[] 等内核数据结构。
        // 若要实现真正的坚固沙盒，必须配置额外的 MPU Region 将内核内存设为 Privileged-only！
        // 作为微内核演示项目，目前接受这一限制，特此声明。
        // ====================================================================================
        // CONTROL: Bit 1 (SPSEL) = 1 (使用 PSP). Bit 0 (nPRIV) = 0 (特权级) or 1 (非特权级)
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

        __asm__ volatile ("cpsid i" : : : "memory"); // 临界区：屏蔽所有中断

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

        __asm__ volatile ("cpsie i" : : : "memory"); // 恢复中断
    }

    // 3. 让当前线程交出 CPU 并休眠指定时间
    void sleep(uint32_t ticks) {
        __asm__ volatile ("cpsid i" : : : "memory");
        TaskControlBlock* current = get_current_tcb();
        current->sleep_ticks = ticks;
        current->state = TaskState::Sleeping;
        __asm__ volatile ("cpsie i" : : : "memory");
        
        schedule(); // 立即触发调度，让出 CPU
    }

    // 4. 供定时器中断调用的时间刷新函数
    void tick_update() {
        __asm__ volatile ("cpsid i" : : : "memory");
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
        __asm__ volatile ("cpsie i" : : : "memory");
    }

    TaskControlBlock* get_current_tcb() { return &tasks[current_task_index]; }

private:
    Scheduler() = default;
    static constexpr int MAX_TASKS = 8;
    TaskControlBlock tasks[MAX_TASKS];
    uint32_t current_task_index = 0;
    uint32_t task_count = 0;
};

#endif