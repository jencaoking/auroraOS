#include "uart.h"

#define SYSCLK 12000000

void uart_init(void)
{
    UART0_CTL = 0;
    UART0_IBRD = SYSCLK / (16 * 115200);
    UART0_FBRD = 0;
    UART0_LCRH = (0x3 << 5);
    UART0_IMSC = 0;
    UART0_CTL = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(char c)
{
    while (UART0_FR & UART_FR_TXFF);
    UART0_DR = c;
}

char uart_getc(void)
{
    while (UART0_FR & UART_FR_RXFE);
    return (char)(UART0_DR & 0xFF);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}
