#include "watch_app.hpp"
#include "power_manager.hpp"
#include "st7789_driver.hpp"
#include "sensor_framework.hpp"
#include "gesture_recognizer.hpp"
#include "ble_stack.hpp"
#include "font_engine.hpp"

// ========================================================
// 静态全局变量与微型显存池
// ========================================================
// 针对 192x490 屏幕，我们不分配全屏显存 (会吃掉 188KB SRAM)
// 而是分配一块 192x64 的局部渲染缓冲 (仅需 ~24KB)，结合 DMA 分块推送
static constexpr uint16_t CHUNK_HEIGHT = 64;
static uint16_t render_buffer[192 * CHUNK_HEIGHT];

// 深色系主题常量 (极致降低 AMOLED 功耗)
static constexpr uint16_t COLOR_BG_DARK    = 0x0821; // 深渊黑
static constexpr uint16_t COLOR_TEXT_ACCENT = 0x07E0; // 极光绿
static constexpr uint16_t COLOR_TEXT_MUTED  = 0x8410; // 碳灰

// ========================================================
// 1. GUI 渲染管线接管 (Watch Face)
// ========================================================
void WatchApp::render_watch_face() {
    // 获取底层感知数据
    SensorData hr_data;
    uint32_t current_bpm = 0;
    if (SensorManager::instance().pop_data(&hr_data) && hr_data.type == SensorType::HEART_RATE) {
        current_bpm = hr_data.payload.bpm;
    }
    uint32_t current_steps = SensorManager::instance().get_accel_sensor().get_steps();
    BleConnectionState ble_state = BleManager::instance().get_state();

    // 采用分块渲染 (Chunked Rendering) 推送至 ST7789
    // Chunk 0: 顶部状态栏 (电量与蓝牙状态)
    St7789Driver::instance().set_window(0, 0, 191, CHUNK_HEIGHT - 1);
    for(int i=0; i<192*CHUNK_HEIGHT; ++i) render_buffer[i] = COLOR_BG_DARK; // 清空背景
    
    if (ble_state == BleConnectionState::CONNECTED) {
        FontEngine::draw_string(150, 10, "BLE", FontColor::BLUE, FontSize::SMALL, render_buffer, 192);
    }
    FontEngine::draw_number(10, 10, 85, FontColor::WHITE, FontSize::SMALL, render_buffer, 192); // 模拟 85% 电量
    St7789Driver::instance().write_patch(render_buffer, 192 * CHUNK_HEIGHT);

    // Chunk 1 & 2: 中心巨大的时间显示
    St7789Driver::instance().set_window(0, CHUNK_HEIGHT, 191, CHUNK_HEIGHT*3 - 1);
    for(int i=0; i<192*(CHUNK_HEIGHT*2); ++i) render_buffer[i] = COLOR_BG_DARK; 
    
    // 渲染时间字符串 (如 10:09)
    char time_str[6];
    time_str[0] = (simulated_time_h_ / 10) + '0';
    time_str[1] = (simulated_time_h_ % 10) + '0';
    time_str[2] = ':';
    time_str[3] = (simulated_time_m_ / 10) + '0';
    time_str[4] = (simulated_time_m_ % 10) + '0';
    time_str[5] = '\0';
    FontEngine::draw_string(20, 20, time_str, FontColor::WHITE, FontSize::HUGE, render_buffer, 192);
    St7789Driver::instance().write_patch(render_buffer, 192 * (CHUNK_HEIGHT*2));

    // Chunk 3: 底部运动健康数据区 (心率 & 步数)
    St7789Driver::instance().set_window(0, CHUNK_HEIGHT*3, 191, 489);
    for(int i=0; i<192*(490 - CHUNK_HEIGHT*3); ++i) render_buffer[i] = COLOR_BG_DARK;

    FontEngine::draw_string(20, 20, "HR:", static_cast<FontColor>(COLOR_TEXT_MUTED), FontSize::MEDIUM, render_buffer, 192);
    FontEngine::draw_number(80, 20, current_bpm, FontColor::RED, FontSize::MEDIUM, render_buffer, 192);

    FontEngine::draw_string(20, 70, "STP:", static_cast<FontColor>(COLOR_TEXT_MUTED), FontSize::MEDIUM, render_buffer, 192);
    FontEngine::draw_number(80, 70, current_steps, static_cast<FontColor>(COLOR_TEXT_ACCENT), FontSize::MEDIUM, render_buffer, 192);

    St7789Driver::instance().write_patch(render_buffer, 192 * (490 - CHUNK_HEIGHT*3));
}

// ========================================================
// 2. 交互状态路由接管 (7 种手势响应)
// ========================================================
void WatchApp::handle_gesture(GestureType gesture) {
    if (gesture == GestureType::NONE) return;

    // 交互防抖：触发任何手势，系统立即重置熄屏倒计时，保持 Active 状态
    PowerManager::instance().transition_to(PowerState::ACTIVE);

    // 状态机页面路由
    switch (current_page_) {
        case WatchPage::WATCH_FACE:
            if (gesture == GestureType::SWIPE_DOWN) {
                current_page_ = WatchPage::QUICK_PANEL; // 下拉呼出控制中心
            } else if (gesture == GestureType::SWIPE_LEFT) {
                current_page_ = WatchPage::HEART_RATE;  // 侧滑进入心率趋势图
            }
            break;

        case WatchPage::HEART_RATE:
        case WatchPage::QUICK_PANEL:
            // 边缘右滑或上滑统一返回主表盘
            if (gesture == GestureType::SWIPE_RIGHT || gesture == GestureType::SWIPE_UP) {
                current_page_ = WatchPage::WATCH_FACE;
            }
            break;
            
        default:
            break;
    }
}

// ========================================================
// 3. 蓝牙与后台数据流同步接管
// ========================================================
void WatchApp::on_background_tick(uint32_t delta_ticks) {
    // 驱动电源生命周期引擎
    PowerManager::instance().on_tick(delta_ticks);

    // 累加时间，驱动表盘 UI 更新
    static uint32_t ms_accumulator = 0;
    ms_accumulator += delta_ticks;
    if (ms_accumulator >= 60000) { 
        ms_accumulator = 0;
        simulated_time_m_++;
        if (simulated_time_m_ >= 60) {
            simulated_time_m_ = 0;
            simulated_time_h_ = (simulated_time_h_ + 1) % 24;
        }
    }

    // 蓝牙 GATT Server 数据同步
    // 获取底层步数算法和心率滤波后的最终可信值，打包推向手机端
    if (BleManager::instance().get_state() == BleConnectionState::CONNECTED) {
        static uint32_t sync_throttle = 0;
        sync_throttle += delta_ticks;
        
        // 限制蓝牙同步频率为 1Hz，防止射频芯片过热并节省电量
        if (sync_throttle >= 1000) {
            sync_throttle = 0;
            
            // 同步心率 (0x180D Service)
            SensorData data;
            if (SensorManager::instance().pop_data(&data) && data.type == SensorType::HEART_RATE) {
                BleManager::instance().update_heart_rate(static_cast<uint8_t>(data.payload.bpm));
            }
            
            // 同步电池电量 (0x180F Service)
            BleManager::instance().update_battery_level(85); // 假设当前电量为 85%
        }
    }
}
