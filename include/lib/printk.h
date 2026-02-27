#ifndef PRINTK_H
#define PRINTK_H

#include <stdarg.h>

/* Early printk - uses direct VGA access (before driver layer is ready) */
void printk_early(const char *fmt, ...);
void vprintk_early(const char *fmt, va_list args);

/* Regular printk - uses driver abstraction layer */
void printk(const char *fmt, ...);
void vprintk(const char *fmt, va_list args);

#endif /* PRINTK_H */
