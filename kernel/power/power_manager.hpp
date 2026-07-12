#ifndef AURORA_POWER_MANAGER_HPP
#define AURORA_POWER_MANAGER_HPP

#include <stdint.h>

// 引入板级配置与传感器框架的空声明（实际项目中应引入真实头文件）
#include "board.h"
#include "../../drivers/sensor/sensor_framework.hpp"
#include "../../drivers/display/st7789_driver.hpp"
#include "../frame_scheduler_v2.hpp"

// ========================================================
// 5 级电源状态定义
// ========================================================
enum class PowerState : uint8_t {
    ACTIVE,     // 亮屏 (100% 亮度)，30fps，传感器全开，功耗 ~15mA
    DIM,        // 暗屏 (30% 亮度)，15fps，传感器全开，功耗 ~8mA
    IDLE,       // 息屏，1fps，传感器低频采集，功耗 ~1mA
    SLEEP,      // 息屏，0fps (暂停调度)，仅保留 Accel 进行抬腕检测，功耗 ~0.1mA
    CRITICAL    // 息屏，0fps，仅保留 RTC 时钟，功耗 ~0.05mA
};

// ========================================================
// 抬腕唤醒检测器 (WristWakeDetector)
// ========================================================
class WristWakeDetector {
private:
    bool     is_looking_at_watch_;
    uint32_t steady_ticks_;

public:
    WristWakeDetector() : is_looking_at_watch_(false), steady_ticks_(0) {}

    // 核心算法：输入 Z 轴加速度值 (单位: mg，1g = 1000mg)
    bool process_accel_z(int32_t z_mg, uint32_t delta_ticks) {
        // 模式识别: 手腕平放看表时，Z轴重力分量从近 0 变为接近 +1g (假设阈值为 800mg ~ 1200mg)
        if (z_mg > 800 && z_mg < 1200) {
            steady_ticks_ += delta_ticks;
            // 1秒防抖过滤，防止手臂日常摆动误触发
            if (steady_ticks_ >= 1000) {
                if (!is_looking_at_watch_) {
                    is_looking_at_watch_ = true;
                    return true; // 成功触发抬腕！
                }
            }
        } else {
            // 姿态破坏，状态重置
            steady_ticks_ = 0;
            is_looking_at_watch_ = false;
        }
        return false;
    }
};

// ========================================================
// 电源管理器核心
// ========================================================
class PowerManager {
private:
    PowerState        current_state_;
    uint32_t          state_ticks_;      // 当前状态已维持的时间 (ms)
    WristWakeDetector wake_detector_;

    // 状态机超时降级阈值 (单位: ms)
    static constexpr uint32_t TIMEOUT_ACTIVE_TO_DIM = 5000;  // 5秒无交互变暗
    static constexpr uint32_t TIMEOUT_DIM_TO_IDLE   = 3000;  // 暗屏3秒后息屏
    static constexpr uint32_t TIMEOUT_IDLE_TO_SLEEP = 10000; // 息屏10秒后进入深度睡眠

    PowerManager() : current_state_(PowerState::ACTIVE), state_ticks_(0) {}

    // 硬件降级与恢复路由机制
    void apply_state_hardware(PowerState state) {
        /*
        // 伪代码：实际调用需对接具体外设驱动
        switch (state) {
            case PowerState::ACTIVE:
                St7789Driver::instance().set_brightness(100);
                FrameSchedulerV2::instance().set_fps(30);
                break;
            case PowerState::DIM:
                St7789Driver::instance().set_brightness(30);
                FrameSchedulerV2::instance().set_fps(15);
                break;
            case PowerState::IDLE:
                St7789Driver::instance().enter_sleep();
                FrameSchedulerV2::instance().set_fps(1);
                break;
            case PowerState::SLEEP:
                FrameSchedulerV2::instance().set_fps(0); // 暂停帧推进
                // SensorManager 关闭心率，仅保留 BHI260AP 计步与抬腕
                break;
            case PowerState::CRITICAL:
                // 关断除 RTC 外所有外设供电
                break;
        }
        */
    }

public:
    static PowerManager& instance() {
        static PowerManager pm;
        return pm;
    }

    PowerState get_state() const { return current_state_; }

    // 强制状态转换 (供触控按键中断、手势引擎或外部通知调用)
    void transition_to(PowerState new_state) {
        if (current_state_ == new_state) return;
        current_state_ = new_state;
        state_ticks_ = 0;
        apply_state_hardware(current_state_);
    }

    // 系统主心跳守护：处理超时降级与休眠期意图检测
    void on_tick(uint32_t delta_ticks) {
        state_ticks_ += delta_ticks;

        // 1. 状态机超时自动降级机制
        switch (current_state_) {
            case PowerState::ACTIVE:
                if (state_ticks_ >= TIMEOUT_ACTIVE_TO_DIM) transition_to(PowerState::DIM);
                break;
            case PowerState::DIM:
                if (state_ticks_ >= TIMEOUT_DIM_TO_IDLE) transition_to(PowerState::IDLE);
                break;
            case PowerState::IDLE:
                if (state_ticks_ >= TIMEOUT_IDLE_TO_SLEEP) transition_to(PowerState::SLEEP);
                break;
            case PowerState::SLEEP:
            case PowerState::CRITICAL:
                break; // 最低功耗状态，由外部中断唤醒
        }

        // 2. 息屏深睡期的抬腕唤醒检测联动
        if (current_state_ == PowerState::IDLE || current_state_ == PowerState::SLEEP) {
            // 占位：从 BHI260AP 获取 Z 轴实时加速度 (单位: mg)
            int32_t z_mg = 0; 

            // 如果满足 1g 防抖抬腕模式识别，瞬间拉起系统到 Active
            if (wake_detector_.process_accel_z(z_mg, delta_ticks)) {
                transition_to(PowerState::ACTIVE);
            }
        }
    }

    // 内核 Idle 线程的最后一道屏障，切断 CPU 供电
    void execute_wfi_if_needed() {
        if (current_state_ == PowerState::SLEEP || current_state_ == PowerState::CRITICAL) {
            // board_enter_wfi(); // 调用 CPU WFI 指令
        }
    }
};

#endif // AURORA_POWER_MANAGER_HPP
