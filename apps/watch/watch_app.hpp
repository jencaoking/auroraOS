#ifndef AURORA_WATCH_APP_HPP
#define AURORA_WATCH_APP_HPP

#include <stdint.h>
// 引入我们在底层已经铺设好的所有核心引擎
#include "power_manager.hpp"
#include "st7789_driver.hpp"
#include "sensor_framework.hpp"
#include "gesture_recognizer.hpp"
#include "ble_stack.hpp"
#include "font_engine.hpp" // 位图字体引擎

// ========================================================
// 手环 UI 页面路由枚举
// ========================================================
enum class WatchPage : uint8_t {
    WATCH_FACE,     // 主表盘 (显示时间、步数、电量)
    HEART_RATE,     // 实时心率测量页
    ACTIVITY,       // 运动数据汇总页
    QUICK_PANEL     // 下拉快捷控制中心
};

class WatchApp {
private:
    WatchPage current_page_;
    uint32_t  simulated_time_h_;
    uint32_t  simulated_time_m_;

    WatchApp() : current_page_(WatchPage::WATCH_FACE), simulated_time_h_(10), simulated_time_m_(9) {}

    // ========================================================
    // 私有 UI 渲染模块 (依赖硬件 ST7789 与位图引擎)
    // ========================================================
    void render_watch_face() {
        // 1. 获取最新传感器数据
        SensorData hr_data, accel_data;
        uint32_t current_bpm = 0;
        uint32_t current_steps = SensorManager::instance().get_accel_sensor().get_steps();

        if (SensorManager::instance().pop_data(&hr_data) && hr_data.type == SensorType::HEART_RATE) {
            current_bpm = hr_data.payload.bpm;
        }

        // 2. 局部清屏 (使用黑色背景节省 AMOLED 功耗)
        // St7789Driver::instance().set_window(0, 0, 192, 490);
        // St7789Driver::instance().write_patch(black_buffer, 192 * 490);

        // 3. 渲染时间 (假设 FontEngine 提供了数字位图绘制能力)
        // FontEngine::draw_string(20, 150, "10:09", FontColor::WHITE, FontSize::HUGE);

        // 4. 渲染步数与心率 Complications
        // FontEngine::draw_string(40, 300, "Steps:", FontColor::GRAY, FontSize::SMALL);
        // FontEngine::draw_number(100, 300, current_steps, FontColor::GREEN, FontSize::SMALL);
        
        // FontEngine::draw_string(40, 350, "HR:", FontColor::GRAY, FontSize::SMALL);
        // FontEngine::draw_number(100, 350, current_bpm, FontColor::RED, FontSize::SMALL);
    }

    void render_heart_rate_page() {
        // 渲染心率专用测量动画与历史折线图
    }

    void render_quick_panel() {
        // 渲染勿扰模式、亮度调节、BLE 开关等控制图标
    }

public:
    static WatchApp& instance() {
        static WatchApp app;
        return app;
    }

    // ========================================================
    // 系统启动时的全量初始化
    // ========================================================
    void init() {
        // 1. 唤醒外设与传感器
        St7789Driver::instance().init();
        SensorManager::instance().init_all();
        
        // 2. 启动蓝牙协议栈并开始广播
        BleManager::instance().init();
        BleManager::instance().start_advertising();
        
        // 3. 强制系统进入亮屏活跃状态
        PowerManager::instance().transition_to(PowerState::ACTIVE);
    }

    // ========================================================
    // 手势路由引擎：处理用户的滑动与点击输入
    // ========================================================
    void handle_gesture(GestureType gesture) {
        if (gesture == GestureType::NONE) return;

        // 任何有效交互都会重置电源状态机的超时计时器，保持 Active 状态
        PowerManager::instance().transition_to(PowerState::ACTIVE);

        switch (current_page_) {
            case WatchPage::WATCH_FACE:
                if (gesture == GestureType::SWIPE_DOWN) {
                    current_page_ = WatchPage::QUICK_PANEL; // 下滑打开快捷面板
                } else if (gesture == GestureType::SWIPE_LEFT) {
                    current_page_ = WatchPage::HEART_RATE;  // 左滑进入心率应用
                }
                break;

            case WatchPage::HEART_RATE:
                if (gesture == GestureType::SWIPE_RIGHT) {
                    current_page_ = WatchPage::WATCH_FACE;  // 右滑返回主表盘
                }
                break;

            case WatchPage::QUICK_PANEL:
                if (gesture == GestureType::SWIPE_UP) {
                    current_page_ = WatchPage::WATCH_FACE;  // 上滑收起快捷面板
                }
                break;

            default:
                break;
        }
    }

    // ========================================================
    // UI 渲染主心跳 (由 FrameSchedulerV2 在允许的帧窗口内调用)
    // ========================================================
    void on_frame_render() {
        // 如果电源管理器处于息屏状态，直接跳过渲染以极限省电
        if (PowerManager::instance().get_state() == PowerState::IDLE ||
            PowerManager::instance().get_state() == PowerState::SLEEP ||
            PowerManager::instance().get_state() == PowerState::CRITICAL) {
            return;
        }

        switch (current_page_) {
            case WatchPage::WATCH_FACE: render_watch_face(); break;
            case WatchPage::HEART_RATE: render_heart_rate_page(); break;
            case WatchPage::QUICK_PANEL: render_quick_panel(); break;
            default: break;
        }
    }

    // ========================================================
    // 后台逻辑主心跳 (处理数据同步与蓝牙分发)
    // ========================================================
    void on_background_tick(uint32_t delta_ticks) {
        // 1. 驱动电源管理器，处理自动息屏与抬腕唤醒
        PowerManager::instance().on_tick(delta_ticks);

        // 2. 模拟时间推移
        static uint32_t time_acc = 0;
        time_acc += delta_ticks;
        if (time_acc >= 60000) { // 每 60 秒增加 1 分钟
            time_acc = 0;
            simulated_time_m_++;
            if (simulated_time_m_ >= 60) {
                simulated_time_m_ = 0;
                simulated_time_h_ = (simulated_time_h_ + 1) % 24;
            }
        }

        // 3. 将最新的传感器数据通过 BLE 推送给手机 App
        // 仅在实际连接状态下推送
        if (BleManager::instance().get_state() == BleConnectionState::CONNECTED) {
            SensorData data;
            if (SensorManager::instance().pop_data(&data)) {
                if (data.type == SensorType::HEART_RATE) {
                    BleManager::instance().update_heart_rate(static_cast<uint8_t>(data.payload.bpm));
                }
            }
        }
    }
};

#endif // AURORA_WATCH_APP_HPP
