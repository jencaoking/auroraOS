#ifndef SEMAPHORE_HPP
#define SEMAPHORE_HPP

#include "task.hpp"

class Semaphore {
private:
    int count_;

    inline void disable_interrupts() { __asm__ volatile ("cpsid i" : : : "memory"); }
    inline void enable_interrupts()  { __asm__ volatile ("cpsie i" : : : "memory"); }

public:
    // 初始化时指定资源的初始数量
    constexpr Semaphore(int init_count = 0) : count_(init_count) {}
    
    void init(int init_count) {
        count_ = init_count;
    }

    // 消费者等待资源
    void wait() {
        while (true) {
            disable_interrupts();
            if (count_ > 0) {
                count_--;
                enable_interrupts();
                return; // 成功获取资源
            }
            enable_interrupts();

            // 没有资源：这里不能只调用 schedule() 空转轮询——
            // 等待中的任务仍处于 Ready 状态，若其优先级较高，会在两阶段调度
            // 算法的"阶段一"里持续被选为最高优先级候选，导致同级/更低优先级
            // 任务（包括真正能释放这个信号量的生产者）被忙等饿死，形成事实上
            // 的优先级反转。改为真正让出 CPU 一个 tick（进入 Sleeping 态），
            // 调度器才会去运行其他任务，1 tick 后自动醒来重试。
            Scheduler::instance().sleep(1);
        }
    }

    // 消费者尝试获取资源：非阻塞版本，资源不足时立即返回 false，不会让出 CPU 或休眠。
    // 可安全用于中断上下文（ISR）—— 与 signal() 一样只用关中断做临界区保护，
    // 不涉及任务调度/系统调用。
    bool try_wait() {
        disable_interrupts();
        if (count_ > 0) {
            count_--;
            enable_interrupts();
            return true;
        }
        enable_interrupts();
        return false;
    }

    // 生产者释放/增加资源
    void signal() {
        disable_interrupts();
        count_++;
        enable_interrupts();
    }
};

#endif
