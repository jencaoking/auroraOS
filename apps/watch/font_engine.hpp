#ifndef AURORA_FONT_ENGINE_HPP
#define AURORA_FONT_ENGINE_HPP

#include <stdint.h>
// #include "st7789_driver.hpp" // 引入底层显示驱动进行像素推送

// ========================================================
// 颜色与尺寸定义 (RGB565 格式)
// ========================================================
enum class FontColor : uint16_t {
    WHITE = 0xFFFF,
    BLACK = 0x0000,
    RED   = 0xF800,
    GREEN = 0x07E0,
    GRAY  = 0x8410,
    BLUE  = 0x001F
};

enum class FontSize : uint8_t {
    SMALL,  // 约 16px 高度，用于显示 "HR:" 或 "Steps:" 标签
    MEDIUM, // 约 24px 高度，用于显示具体数值
    HUGE    // 约 48px 高度，用于显示主表盘的 "10:09" 时钟大字
};

// ========================================================
// 字体字模描述结构体 (对齐 LVGL 导出格式)
// ========================================================
struct GlyphInfo {
    uint8_t  width;       // 字符宽度
    uint8_t  height;      // 字符高度
    int8_t   x_offset;    // X 轴绘制偏移
    int8_t   y_offset;    // Y 轴绘制偏移
    uint32_t bitmap_idx;  // 在一维字模数组中的起始索引
};

struct FontDef {
    const uint8_t*   bitmap_data; // 存储在 Flash 中的字模像素点阵数据 (1bpp 或 4bpp)
    const GlyphInfo* glyphs;      // 字符映射表
    uint8_t          line_height; // 行高
    char             start_char;  // 支持的起始 ASCII 码 (如 ' ')
    char             end_char;    // 支持的结束 ASCII 码 (如 '~')
};

// ========================================================
// 位图渲染引擎核心
// ========================================================
class FontEngine {
private:
    // 占位：实际开发中，这些外部变量将由外部生成的 .cpp 字体数据文件提供
    // extern const FontDef font_small_16;
    // extern const FontDef font_huge_48;

    // 内部微型辅助：根据 FontSize 获取对应的字体数据定义
    static const FontDef* get_font_def(FontSize size) {
        // switch (size) {
        //     case FontSize::SMALL:  return &font_small_16;
        //     case FontSize::HUGE:   return &font_huge_48;
        //     default:               return &font_small_16;
        // }
        return nullptr;
    }

public:
    // ========================================================
    // 渲染单个字符
    // ========================================================
    static uint16_t draw_char(uint16_t x, uint16_t y, char c, FontColor color, FontSize size = FontSize::MEDIUM, uint16_t* buffer = nullptr, uint16_t buffer_width = 0) {
        const FontDef* font = get_font_def(size);
        if (!font || c < font->start_char || c > font->end_char) return 0;

        uint16_t glyph_idx = c - font->start_char;
        const GlyphInfo& glyph = font->glyphs[glyph_idx];

        // 核心渲染逻辑：
        // 1. 设置 ST7789 硬件 DMA 接收窗口：
        //    St7789Driver::instance().set_window(x + glyph.x_offset, y + glyph.y_offset, 
        //                                        x + glyph.x_offset + glyph.width - 1, 
        //                                        y + glyph.y_offset + glyph.height - 1);
        
        // 2. 解析 font->bitmap_data 中的 1bpp 点阵数据，如果是 1 则填充 color，如果是 0 则跳过或填背景色
        //    为了追求极致性能，这里应当构建一个局部显存 Buffer，解析完毕后调用 write_patch 批量推送

        // 3. 返回光标应向前移动的宽度，以便连续绘制下一个字符
        return glyph.width + 2; // +2 为字符间距
    }

    // ========================================================
    // 渲染字符串
    // ========================================================
    static void draw_string(uint16_t x, uint16_t y, const char* str, FontColor color, FontSize size = FontSize::MEDIUM, uint16_t* buffer = nullptr, uint16_t buffer_width = 0) {
        if (!str) return;
        uint16_t cursor_x = x;
        
        while (*str) {
            // 逐字绘制，并累加光标 X 轴偏移量
            cursor_x += draw_char(cursor_x, y, *str, color, size, buffer, buffer_width);
            str++;
        }
    }

    // ========================================================
    // 渲染整数数值 (零内存分配)
    // ========================================================
    static void draw_number(uint16_t x, uint16_t y, int32_t num, FontColor color, FontSize size = FontSize::MEDIUM, uint16_t* buffer = nullptr, uint16_t buffer_width = 0) {
        char buf[16];
        int i = 0;
        bool is_neg = false;

        if (num == 0) {
            buf[i++] = '0';
        } else {
            if (num < 0) {
                is_neg = true;
                num = -num;
            }
            while (num > 0) {
                buf[i++] = (num % 10) + '0';
                num /= 10;
            }
            if (is_neg) buf[i++] = '-';
        }
        buf[i] = '\0';

        // 逆序排列缓冲区，因为上述算法算出来的是个位在最前面
        int start = 0;
        int end = i - 1;
        while (start < end) {
            char temp = buf[start];
            buf[start] = buf[end];
            buf[end] = temp;
            start++;
            end--;
        }

        draw_string(x, y, buf, color, size, buffer, buffer_width);
    }
};

#endif // AURORA_FONT_ENGINE_HPP
