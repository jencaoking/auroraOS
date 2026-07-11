#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_NAME "auroraOS"
#define KERNEL_VERSION "0.2.0"
#define KERNEL_ARCH "Cortex-M4"

// 注：SYSCLK / UART_BAUDRATE 已迁入 boards/<board>/board.h (BOARD_SYSCLK_FREQ /
// BOARD_UART_BAUDRATE)，避免板级硬件参数出现两处来源。FLASH/RAM/STACK 因链接
// 脚本符号对 C 不可见，仍保留于此作为 C 层常量。

#define FLASH_SIZE (256 * 1024)
#define RAM_SIZE (64 * 1024)

#define STACK_SIZE 0x2000

#endif
