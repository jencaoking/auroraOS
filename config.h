#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_NAME "auroraOS"
#define KERNEL_VERSION "0.2.0"
#define KERNEL_ARCH "Cortex-M4"

#define SYSCLK_FREQ 12000000
#define UART_BAUDRATE 115200

#define FLASH_SIZE (256 * 1024)
#define RAM_SIZE (64 * 1024)

#define STACK_SIZE 0x2000

#endif
