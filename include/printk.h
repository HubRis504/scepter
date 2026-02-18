#ifndef PRINTK_H
#define PRINTK_H

#include <stdarg.h>

/* printf-style kernel print – outputs via vga_putchar */
void printk(const char *fmt, ...);
void vprintk(const char *fmt, va_list args);

#endif /* PRINTK_H */
