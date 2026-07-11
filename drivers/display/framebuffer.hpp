#ifndef AURORA_FRAMEBUFFER_HPP
#define AURORA_FRAMEBUFFER_HPP

#include <stdint.h>
#include "oled_driver.hpp"

// 脏区域包围盒类
struct DirtyRect {
    uint16_t x0, y0, x1, y1;
    bool     is_dirty;

    void reset() {
        x0 = y0 = x1 = y1 = 0;
        is_dirty = false;
    }

    // 自动求取区域并集：每次绘制图形，自动扩张脏矩形边界
    void union_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
        if (w == 0 || h == 0) return;
        uint16_t right = x + w - 1;
        uint16_t bottom = y + h - 1;

        if (!is_dirty) {
            x0 = x; y0 = y;
            x1 = right; y1 = bottom;
            is_dirty = true;
        } else {
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (right > x1) x1 = right;
            if (bottom > y1) y1 = bottom;
        }
    }
};

// 带有脏区域跟踪能力的超级渲染缓冲树
template <uint16_t Width, uint16_t Height>
class FrameBuffer {
private:
    ColorRGB565 buffer_[Width * Height];
    DirtyRect   dirty_;
    
    // 临时的行块发送缓冲区，避免大块内存分配
    static constexpr uint16_t PATCH_LINE_BUF_SIZE = Width * 4;
    ColorRGB565 line_buffer_[PATCH_LINE_BUF_SIZE];

public:
    FrameBuffer() {
        clear(0x0000); // 默认清屏纯黑
        dirty_.reset();
    }

    // ========================================================
    // 2D 图形基础原语：打点 (自动追踪脏点)
    // ========================================================
    void set_pixel(uint16_t x, uint16_t y, ColorRGB565 color) {
        if (x >= Width || y >= Height) return;
        
        buffer_[y * Width + x] = color;
        dirty_.union_rect(x, y, 1, 1); // 自动跟踪变更！
    }

    // ========================================================
    // 2D 图形原语：矩形填充 (自动追踪区域)
    // ========================================================
    void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, ColorRGB565 color) {
        if (x >= Width || y >= Height) return;
        if (x + w > Width) w = Width - x;
        if (y + h > Height) h = Height - y;

        for (uint16_t row = y; row < y + h; row++) {
            for (uint16_t col = x; col < x + w; col++) {
                buffer_[row * Width + col] = color;
            }
        }
        dirty_.union_rect(x, y, w, h); // 自动合围脏矩形！
    }

    // 简单绘制 ASCII 字符 (6x8 点阵放大简化示意)
    void draw_char(uint16_t x, uint16_t y, char c, ColorRGB565 color) {
        // 为模拟 UI 组件，直接在字符相应区域画一个带有高亮的色块替代复杂字模
        fill_rect(x, y, 8, 12, color);
    }

    // ========================================================
    // 核心输出引擎：将脏区域同步给物理 OLED 屏
    // ========================================================
    void flush(OledDriver& driver) {
        if (!dirty_.is_dirty) return; // 如果画面没有任何变动，0 耗时跳过传输！

        // 1. 设定 OLED 驱动 IC 的硬件局部显示窗口
        driver.set_window(dirty_.x0, dirty_.y0, dirty_.x1, dirty_.y1);

        // 2. 将脏区域内的像素一行行提取并以 DMA/SPI 传输给硬件
        uint16_t patch_width = dirty_.x1 - dirty_.x0 + 1;
        uint16_t patch_height = dirty_.y1 - dirty_.y0 + 1;
        uint32_t total_patch_pixels = patch_width * patch_height;

        // 简化的批量推送演示（直接将需要发送的局部切片指针或拷贝块推送）
        driver.write_patch(&buffer_[dirty_.y0 * Width + dirty_.x0], total_patch_pixels);

        // 3. 画面同步完毕，重置脏矩形树！
        dirty_.reset();
    }

    void clear(ColorRGB565 color) {
        for (uint32_t i = 0; i < Width * Height; i++) {
            buffer_[i] = color;
        }
        dirty_.union_rect(0, 0, Width, Height);
    }
};

#endif
