#ifndef TASK_HPP
#define TASK_HPP

#include <stdint.h>
#include <stddef.h>
#include "arch_api.hpp" // 引入底层架构 HAL 接口

// ============================================================
// 1. 定义标准 RTOS 优先级阶梯 (数值越大，优先级越高)
//    遵循 C++ Core Guidelines Enum.3: 使用 enum class 强类型枚举
// ============================================================
enum class TaskPriority : uint8_t {
    Idle     = 0,  // 最低优先级：仅供系统空闲进程使用
    Low      = 1,  // 低优先级：后台计算、非实时操作
    Normal   = 2,  // 默认优先级：普通前台业务
    High     = 3,  // 高优先级：交互 Shell 等
    Realtime = 4   // 最高硬实时优先级：底层网络帧拦截等
};

enum class TaskState {
    Ready,
    Running,
    Sleeping,
    Blocked_On_Notify
};

// POSIX 标准信号定义
constexpr int SIGINT  = 2;  // 中断信号 (Ctrl+C)
constexpr int SIGKILL = 9;  // 强制终止
constexpr int SIGALRM = 14; // 定时器超时报警
constexpr int SIGUSR1 = 10; // 用户自定义信号 1

using SignalHandler = void (*)(int sig);

// TaskControlBlock: POD 结构体，保存任务的完整上下文快照
// 遵循 C.2: 若需要不变量则使用 class，此处为纯数据故用 struct
struct TaskControlBlock {
    uint32_t*    stack_ptr;       // 任务当前栈顶指针（由 PendSV 保存/恢复）
    void         (*entry_point)(); // 任务入口函数（供 start() 引导跳入第一个任务用）
    TaskState    state;           // 任务状态机
    uint32_t     id;              // 任务唯一 ID
    uint32_t     sleep_ticks;     // 剩余休眠 Tick 数
    TaskPriority base_priority;   // 基础优先级
    TaskPriority current_priority;// 动态优先级（用于优先级继承）
    uint32_t     stack_base;      // 栈基址（用于 MPU）
    uint8_t      size_pow2;       // 栈大小的 2 的幂次方（用于 MPU）

    // ========================================================
    // 1. 【FreeRTOS 任务通知】零开销 TCB 内置字段
    // ========================================================
    uint32_t  notify_value;     // 32 位专有通知值
    bool      notify_pending;   // 是否有未处理的通知

    // ========================================================
    // 2. 【小米 Vela POSIX 信号】异步中断字段
    // ========================================================
    uint32_t      pending_signals;          // 待处理信号位图
    SignalHandler signal_handlers[16];      // 信号回调处理函数表
};

// 前向声明：供 PendSV 汇编读取的两个全局 TCB 指针
// 遵循 I.2: 最小化非 const 全局变量，此处为架构必需
extern "C" {
    extern TaskControlBlock* volatile g_current_tcb_ptr;
    extern TaskControlBlock* volatile g_next_tcb_ptr;
}

class Scheduler {
public:
    // 单例：遵循 I.3，避免多实例造成调度器状态不一致
    static Scheduler& instance() {
        static Scheduler sched;
        return sched;
    }

    void init() {
        current_task_index = 0;
        task_count = 0;
        started_ = false;
    }

    // 创建任务时指定优先级（默认 Normal），遵循 F.15: 提供具名参数
    // 返回值：成功时返回新任务的 TCB 指针（供调用方获取其真实句柄，
    // 例如 lwIP sys_thread_new() 需要返回"新创建线程"而非当前线程的句柄）；
    // 任务表已满（达到 MAX_TASKS）时返回 nullptr，调用方必须检查该返回值，
    // 不能像过去那样静默吞掉创建失败。
    TaskControlBlock* create_task(void (*task_entry)(void),
                     uint32_t* stack_space,
                     uint32_t  stack_size,
                     TaskPriority prio = TaskPriority::Normal,
                     uint8_t size_pow2 = 0) { // 默认为 0 表示未指定
        if (task_count >= MAX_TASKS) return nullptr;

        TaskControlBlock& tcb = tasks[task_count];
        tcb.id          = task_count;
        tcb.state       = TaskState::Ready;
        tcb.sleep_ticks = 0;
        tcb.base_priority = prio;
        tcb.current_priority = prio;
        tcb.entry_point = task_entry;
        tcb.stack_base = reinterpret_cast<uint32_t>(stack_space);
        tcb.size_pow2 = size_pow2;

        // 初始化任务通知与信号管理
        tcb.notify_value = 0;
        tcb.notify_pending = false;
        tcb.pending_signals = 0;
        for (int i = 0; i < 16; i++) tcb.signal_handlers[i] = nullptr;

        // 调用 HAL 接口完成 Cortex-M4 栈帧伪造，与具体架构解耦
        tcb.stack_ptr = Arch::init_thread_stack(task_entry, stack_space, stack_size);
        task_count++;
        return &tcb;
    }

    // ========================================================
    // 【核心改造】调度器在任务切换时，自动检查并分发待处理信号
    // ========================================================
    void dispatch_signals(TaskControlBlock* tcb) {
        if (tcb->pending_signals == 0) return;

        for (int sig = 1; sig < 16; sig++) {
            if (tcb->pending_signals & (1 << sig)) {
                // 清除待处理位
                tcb->pending_signals &= ~(1 << sig);

                // 如果是 SIGKILL，强制无条件终止该线程！
                if (sig == SIGKILL) {
                    tcb->state = TaskState::Sleeping; // 简单标记为挂起/销毁
                    return;
                }

                // 如果注册了回调，立即在当前线程上下文中执行异步处理
                if (tcb->signal_handlers[sig]) {
                    tcb->signal_handlers[sig](sig);
                }
            }
        }
    }

    // =========================================================================
    // 基于优先级的抢占式调度算法 — O(N) 两阶段查找
    //
    // 阶段一: 扫描全部就绪任务，找出当前最高优先级 max_prio
    // 阶段二: 在同属 max_prio 的候选任务中做循环时间片轮转
    // 阶段三: 若选出的任务与当前不同，通过 HAL 触发 PendSV 硬件上下文切换
    // =========================================================================
    void schedule() {
        if (!started_ || task_count <= 1) return;

        // ── 阶段一：寻找最高可运行优先级 ──────────────────────────────────
        TaskPriority max_prio = TaskPriority::Idle;
        for (uint32_t i = 0; i < task_count; i++) {
            if (tasks[i].state != TaskState::Sleeping &&
                tasks[i].current_priority > max_prio) {
                max_prio = tasks[i].current_priority;
            }
        }

        // ── 阶段二：同级优先级循环时间片轮转 ────────────────────────────
        uint32_t next_task = current_task_index;
        for (uint32_t i = 0; i < task_count; i++) {
            next_task = (next_task + 1) % task_count;
            if (tasks[next_task].state != TaskState::Sleeping &&
                tasks[next_task].current_priority == max_prio) {
                break;
            }
        }

        // ── 阶段三：发起上下文切换 ──────────────────────────────────────
        if (next_task != current_task_index) {
            Arch::disable_interrupts(); // 临界区：更新 TCB 指针必须原子完成
            g_current_tcb_ptr = &tasks[current_task_index];
            current_task_index = next_task;
            g_next_tcb_ptr = &tasks[current_task_index];
            
            // 【信号拦截点】切入新任务前，优先处理它的 POSIX 异步信号
            dispatch_signals(&tasks[current_task_index]);
            Arch::enable_interrupts();  // 必须先恢复中断，PendSV 才能被硬件响应
            Arch::trigger_context_switch(); // Pending PendSV 位，等待中断开放后执行
        }
    }

    // 主动休眠：将当前任务挂起，立刻调度次高优先级任务接管 CPU
    void sleep(uint32_t ticks) {
        // 注意：sleep() 仅在任务上下文调用，无需屏蔽中断
        // SysTick 只会检查 sleeping 状态，不会修改当前任务的字段
        TaskControlBlock* current = get_current_tcb();
        current->sleep_ticks = ticks;
        current->state = TaskState::Sleeping;
        schedule(); // 状态更新后立即让出 CPU
    }

    // 由 SysTick 中断调用：滴答计数器驱动唤醒逻辑
    void tick_update() {
        for (uint32_t i = 0; i < task_count; i++) {
            if (tasks[i].state == TaskState::Sleeping) {
                if (tasks[i].sleep_ticks > 0) {
                    tasks[i].sleep_ticks--;
                }
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = TaskState::Ready;
                }
            }
        }
    }

    // 遵循 F.16: 返回裸指针仅表示非所有权观察（调度器拥有 TCB 数组）
    TaskControlBlock* get_current_tcb() { return &tasks[current_task_index]; }

    int get_task_count() const { return task_count; }
    TaskControlBlock* get_task(int index) { 
        if (index >= 0 && index < task_count) return &tasks[index]; 
        return nullptr;
    }
    TaskControlBlock* get_task_by_id(uint32_t id) {
        if (id < task_count) return &tasks[id];
        return nullptr;
    }

    // =========================================================================
    // start(): 从特权 main 上下文启动调度器，跳入第一个任务
    // 架构相关的 PSP/CONTROL 切换与栈帧恢复已封装在 Arch::start_first_task()
    // 中，调度器逻辑层不再感知具体异常返回码或内联汇编
    // =========================================================================
    [[noreturn]] void start() {
        started_ = true;
        g_current_tcb_ptr = &tasks[current_task_index];
        g_next_tcb_ptr    = &tasks[current_task_index];

        // 配置 SysTick 系统心跳（1000Hz → 每 1ms 一次中断）
        // 必须在 start_first_task() 内部的 cpsie i 之前完成：
        // 此时全局中断仍关闭，配置安全；开中断后 SysTick 立即开始产生周期心跳
#ifdef CONFIG_TICK_RATE_HZ
        Arch::systick_init(CONFIG_TICK_RATE_HZ);
#else
        Arch::systick_init(1000);
#endif

        Arch::start_first_task(g_current_tcb_ptr->stack_ptr,
                               tasks[current_task_index].entry_point);
    }

private:
    Scheduler() = default;
#ifdef CONFIG_MAX_TASKS
    static constexpr int MAX_TASKS = CONFIG_MAX_TASKS;
#else
    static constexpr int MAX_TASKS = 16;
#endif
    TaskControlBlock tasks[MAX_TASKS]{};
    uint32_t current_task_index = 0;
    uint32_t task_count = 0;
    bool started_ = false;
};

#endif // TASK_HPP