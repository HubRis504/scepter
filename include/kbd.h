#ifndef KBD_H
#define KBD_H

#include <stdint.h>

/**
 * PS/2 Keyboard Driver
 * 
 * Provides keyboard input via IRQ1 interrupt handler.
 * Characters are buffered in a circular buffer and retrieved via read().
 * Supports basic ASCII input with modifier keys (Shift, Caps Lock).
 */

/**
 * Initialize keyboard driver
 * Sets up IRQ1 handler and initializes buffer
 */
void kbd_init(void);

/**
 * Register keyboard as character device 3
 */
int kbd_register_driver(void);

#endif /* KBD_H */