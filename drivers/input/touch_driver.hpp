#ifndef AURORA_TOUCH_DRIVER_HPP
#define AURORA_TOUCH_DRIVER_HPP

#include <stdint.h>
#include "device.hpp"
#include "input_event.hpp"
#include "task.hpp"

// 模拟 FT6236 手表电容触控芯片驱动
class TouchDriver : public CharDevice {
private:
    uint16_t max_x_;
    uint16_t max_y_;
    
    // QEMU 仿真环境下的内部自动拖拽状态机
    uint16_t sim_x_;
    uint16_t sim_y_;
    int      sim_step_;
    bool     sim_active_;

public:
    TouchDriver(const char* name, uint16_t max_x = 128, uint16_t max_y = 128)
        : CharDevice(name), max_x_(max_x), max_y_(max_y),
          sim_x_(30), sim_y_(30), sim_step_(0), sim_active_(true) {}

    int open() override {
        // 物理硬件：向 I2C 寄存器 0x00 写入复位指令，配置中断触发引脚 (INT)
        return 0;
    }

    // ========================================================
    // 核心接口：UI 系统调用 read() 获取最新触控数据包
    // ========================================================
    int read(char* buf, int len, int offset) override {
        if (len < static_cast<int>(sizeof(TouchPoint))) return 0;
        
        TouchPoint* point = reinterpret_cast<TouchPoint*>(buf);

        // 物理硬件：通过 I2C 读取芯片寄存器 0x02~0x06 (触控点数量、X坐标高低位、Y坐标高低位)
        // 这里我们通过 QEMU 内置生成器，模拟一次极度真实的手指拖拽轨迹：
        // 手指从 (30, 30) 移动到 (90, 90)，步长为 2 像素
        if (sim_active_) {
            point->x = sim_x_;
            point->y = sim_y_;
            
            if (sim_step_ == 0) {
                point->state = TouchState::PRESSED; // 手指刚接触屏幕
            } else if (sim_step_ < 30) {
                point->state = TouchState::MOVING;  // 手指正在拖拽图标
                sim_x_ += 2;
                sim_y_ += 2;
            } else {
                point->state = TouchState::RELEASED;// 手指离开屏幕
                sim_active_ = false;                // 结束本轮模拟拖拽
            }
            point->is_valid = true;
            sim_step_++;
        } else {
            point->is_valid = false;
        }

        return sizeof(TouchPoint);
    }
};

#endif
