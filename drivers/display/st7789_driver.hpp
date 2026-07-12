#ifndef AURORA_ST7789_DRIVER_HPP
#define AURORA_ST7789_DRIVER_HPP

#include <stdint.h>
#include "board.h"   // 引入板级引脚定义 (DISPLAY_WIDTH, DISPLAY_HEIGHT)
// #include "device.hpp"

// ========================================================
// ST7789 核心硬件指令集
// ========================================================
#define ST7789_SWRESET 0x01
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_WRDISBV 0x51 // 写入显示屏背光亮度指令

class St7789Driver {
private:
    uint16_t width_;
    uint16_t height_;
    bool     is_sleeping_;
    uint8_t  current_brightness_;

    // 内部底层接口：发送命令与数据 (实际需对接 Apollo3 SPI 寄存器)
    void spi_send_cmd(uint8_t cmd) {
        // gpio_clear(PIN_DISP_DC);
        // spi_transmit(DISPLAY_SPI_PORT, &cmd, 1);
    }

    void spi_send_data(uint8_t data) {
        // gpio_set(PIN_DISP_DC);
        // spi_transmit(DISPLAY_SPI_PORT, &data, 1);
    }

    void spi_send_data_16(uint16_t data) {
        // gpio_set(PIN_DISP_DC);
        // uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
        // spi_transmit(DISPLAY_SPI_PORT, buf, 2);
    }

    St7789Driver() : width_(192), height_(490), is_sleeping_(false), current_brightness_(100) {} // 适配 192x490 分辨率

public:
    static St7789Driver& instance() {
        static St7789Driver driver;
        return driver;
    }

    // ========================================================
    // 硬件初始化
    // ========================================================
    void init() {
        // 1. 硬件复位
        // gpio_clear(PIN_DISP_RST); delay_ms(10);
        // gpio_set(PIN_DISP_RST); delay_ms(120);

        // 2. 发送出厂初始化序列
        spi_send_cmd(ST7789_SWRESET);
        // delay_ms(150);
        
        spi_send_cmd(ST7789_SLPOUT);
        // delay_ms(120);

        // (此处省略具体的颜色格式、方向扫描等长串配置指令...)

        spi_send_cmd(ST7789_DISPON);
        set_brightness(100);
        is_sleeping_ = false;
    }

    // ========================================================
    // 核心电源管理接口 (供 PowerManager 联动调用)
    // ========================================================
    
    // 进入深度休眠：关闭显示面板，关闭内部升压电路
    void enter_sleep() {
        if (is_sleeping_) return;
        spi_send_cmd(ST7789_DISPOFF);
        spi_send_cmd(ST7789_SLPIN);
        is_sleeping_ = true;
    }

    // 退出深度休眠：唤醒显示面板
    void exit_sleep() {
        if (!is_sleeping_) return;
        spi_send_cmd(ST7789_SLPOUT);
        // delay_ms(120); // 必须等待内部振荡器稳定
        spi_send_cmd(ST7789_DISPON);
        is_sleeping_ = false;
    }

    // 设置屏幕背光亮度 (1-100)
    void set_brightness(uint8_t level) {
        if (level > 100) level = 100;
        current_brightness_ = level;
        
        // ST7789 硬件支持直接通过指令调节 AMOLED 亮度
        uint8_t hw_val = (level * 255) / 100;
        spi_send_cmd(ST7789_WRDISBV);
        spi_send_data(hw_val);
    }

    // 供 Dim 状态快速调用
    void set_low_brightness() {
        set_brightness(30); // Dim 状态亮度 30%
    }

    // ========================================================
    // 动态脏区域渲染引擎接口
    // ========================================================
    
    // 设定硬件写入窗口
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        spi_send_cmd(ST7789_CASET);
        spi_send_data_16(x0);
        spi_send_data_16(x1);

        spi_send_cmd(ST7789_RASET);
        spi_send_data_16(y0);
        spi_send_data_16(y1);

        spi_send_cmd(ST7789_RAMWR);
    }

    // 通过 DMA 进行大块显存补丁推送
    void write_patch(const uint16_t* buffer, uint32_t pixel_count) {
        if (is_sleeping_ || pixel_count == 0) return;

        // gpio_set(PIN_DISP_DC);
        
        // 核心优化：启动 SPI DMA 异步传输
        // spi_dma_transmit_async(DISPLAY_SPI_PORT, (uint8_t*)buffer, pixel_count * 2);
        
        // 传输期间，通知 CPU 直接陷入 WFI 浅度睡眠节省电能，直到 DMA 传输完成中断唤醒 CPU
        // __asm__ volatile ("wfi" : : : "memory"); 
    }

    uint16_t get_width() const { return width_; }
    uint16_t get_height() const { return height_; }
};

#endif // AURORA_ST7789_DRIVER_HPP
