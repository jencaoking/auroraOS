#ifndef AURORA_POWER_MANAGER_WRAPPER_HPP
#define AURORA_POWER_MANAGER_WRAPPER_HPP

#include "config/autoconf.h"

#ifdef CONFIG_BOARD_MIBAND8
#include "power/power_manager.hpp"
#else

#include <stdint.h>
#include "task.hpp"

// 对标蓝河 BlueOS 的 5 级功耗状态 (QEMU 模拟版)
enum class PowerState : uint8_t {
    RUN         = 0, // 全速运行：CPU 全频，外设全开
    IDLE        = 1, // 浅度空闲：CPU 停止 (WFI)，SysTick 保持 1ms 运行
    LIGHT_SLEEP = 2, // 轻度睡眠：CPU 停止，关闭高频时钟，开启 Tickless
    DEEP_SLEEP  = 3, // 深度睡眠：仅保留 RTC 唤醒，大部分 RAM 掉电
    SHUTDOWN    = 4  // 关机模式：仅按键中断可唤醒
};

class PowerManager {
private:
    PowerState current_state_;
    uint32_t   idle_time_ms_; // 记录系统处于空闲状态的持续时间

    inline void disable_interrupts() { __asm__ volatile ("cpsid i" : : : "memory"); }
    inline void enable_interrupts()  { __asm__ volatile ("cpsie i" : : : "memory"); }
    inline void wait_for_interrupt() { __asm__ volatile ("wfi" : : : "memory"); }

public:
    static PowerManager& instance() {
        static PowerManager pm;
        return pm;
    }

    PowerManager() : current_state_(PowerState::RUN), idle_time_ms_(0) {}

    // 核心接口：由系统的最低优先级守护线程 (Idle Task) 持续调用
    void enter_idle_state(uint32_t expected_idle_ticks) {
        disable_interrupts();

        // 1. 如果预计空闲时间极短（如小于 5ms），不值得折腾，直接进入最浅的 WFI
        if (expected_idle_ticks < 5) {
            current_state_ = PowerState::IDLE;
            wait_for_interrupt(); // CPU 休眠，但会被 1ms 后的 SysTick 立即叫醒
            enable_interrupts();
            return;
        }

        // 2. 如果空闲时间较长，触发极度省电的 Tickless 机制！
        current_state_ = PowerState::LIGHT_SLEEP;

        // a. 停止当前的 1ms SysTick 硬件中断
        volatile uint32_t* syst_ctrl = reinterpret_cast<volatile uint32_t*>(0xE000E010);
        *syst_ctrl &= ~(1 << 0); // 清除 ENABLE 位

        // b. 配置一个由低速 LPTIMER 或 RTC 驱动的唤醒定时器（QEMU 下以逻辑模拟）
        // setup_lp_timer_wakeup(expected_idle_ticks);

        // c. CPU 彻底陷入深度休眠，时钟树停止翻转
        wait_for_interrupt(); 

        // ======================= [漫长的睡眠过去，CPU 被外部中断或唤醒定时器叫醒] =======================

        // d. 醒来后，立刻恢复 1ms SysTick 硬件中断
        *syst_ctrl |= (1 << 0);

        // e. 【关键】计算刚才睡了多久，并把缺失的 Ticks 补偿给调度器 and 软件定时器！
        // uint32_t slept_ticks = get_lp_timer_elapsed();
        uint32_t slept_ticks = expected_idle_ticks; // 简单模拟：假设完美睡到了预期时间
        
        // 我们需要通知调度器时间已经流逝
        Scheduler::instance().compensate_ticks(slept_ticks);

        current_state_ = PowerState::RUN;
        enable_interrupts();
    }
};

#endif // CONFIG_BOARD_MIBAND8

#endif // AURORA_POWER_MANAGER_WRAPPER_HPP
