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
    void render_watch_face();

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
    void handle_gesture(GestureType gesture);

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
    void on_background_tick(uint32_t delta_ticks);
};

#endif // AURORA_WATCH_APP_HPP
