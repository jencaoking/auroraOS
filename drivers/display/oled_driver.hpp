#ifndef AURORA_OLED_DRIVER_HPP
#define AURORA_OLED_DRIVER_HPP

#include <stdint.h>
#include "device.hpp"
#include "posix.hpp"

// 16位 RGB565 颜色定义
using ColorRGB565 = uint16_t;

class OledDriver : public CharDevice {
private:
    uint16_t width_;
    uint16_t height_;
    int      console_fd_;

public:
    OledDriver(const char* name, uint16_t width = 128, uint16_t height = 128) 
        : CharDevice(name), width_(width), height_(height), console_fd_(-1) {}

    int open() override {
        // 在实际物理硬件中，这里会初始化 SPI0 控制器、复位引脚及 OLED 驱动 IC 寄存器
        console_fd_ = ::open("/dev/uart0", 0);
        return 0;
    }

    // ========================================================
    // OLED 硬件核心能力：设定局部显存写入窗口 (x0, y0) -> (x1, y1)
    // ========================================================
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        // 物理硬件：发送指令 0x2A 设定列地址，发送 0x2B 设定行地址，最后发 0x2C 准备写像素
        if (console_fd_ >= 0) {
            char msg[64];
            int len = 0;
            // 简单格式化打印我们的 SPI 硬件指令抓包
            auto append_str = [&](const char* s) { while (*s) msg[len++] = *s++; };
            auto append_num = [&](uint16_t n) {
                char tmp[8]; int i = 0;
                if (n == 0) tmp[i++] = '0';
                while (n > 0) { tmp[i++] = (n % 10) + '0'; n /= 10; }
                while (i > 0) msg[len++] = tmp[--i];
            };

            append_str("[SPI CMD] Set Window: (");
            append_num(x0); append_str(", "); append_num(y0);
            append_str(") -> (");
            append_num(x1); append_str(", "); append_num(y1);
            append_str(")\r\n");
            ::write(console_fd_, msg, len);
        }
    }

    // ========================================================
    // 局域数据推送：仅将变动矩形内的显存补丁以 DMA/SPI 传输
    // ========================================================
    void write_patch(const ColorRGB565* buffer, uint32_t pixel_count) {
        // 物理硬件：启动 DMA 传输 pixel_count * 2 个字节到 SPI 数据寄存器
        if (console_fd_ >= 0) {
            char msg[64];
            int len = 0;
            auto append_str = [&](const char* s) { while (*s) msg[len++] = *s++; };
            auto append_num = [&](uint32_t n) {
                char tmp[10]; int i = 0;
                if (n == 0) tmp[i++] = '0';
                while (n > 0) { tmp[i++] = (n % 10) + '0'; n /= 10; }
                while (i > 0) msg[len++] = tmp[--i];
            };

            append_str("    ⚡ [SPI DMA] Flushed Patch Size: ");
            append_num(pixel_count * sizeof(ColorRGB565));
            append_str(" Bytes (");
            
            // 计算比起全量刷新的带宽节省比例
            uint32_t total_bytes = width_ * height_ * sizeof(ColorRGB565);
            uint32_t saved_pct = 100 - ((pixel_count * sizeof(ColorRGB565) * 100) / total_bytes);
            append_num(saved_pct);
            append_str("% Bandwidth Saved!)\r\n");
            ::write(console_fd_, msg, len);
        }
    }

    uint16_t get_width() const { return width_; }
    uint16_t get_height() const { return height_; }
};

#endif
