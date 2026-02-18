#include "printk.h"
#include "vga.h"

void printk(const char *str)
{
    while (*str)
        vga_putchar(*str++);
}
