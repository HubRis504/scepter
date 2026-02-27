#include "driver/char/pit.h"
#include "driver/pic.h"
#include "kernel/cpu.h"
#include "printk.h"
#include "driver/driver.h"
#include "asm.h"

/* =========================================================================
 * Driver state
 * ========================================================================= */

static volatile uint32_t pit_ticks = 0;

uint32_t pit_get_ticks(void)
{
    return pit_ticks;
}

/* =========================================================================
 * IRQ0 handler – called from the irq0 stub in isr.s
 * ========================================================================= */

void pit_isr(void)
{
    pit_ticks++;

    // if (pit_ticks % 100 == 0)
    //     printk("[PIT] tick %u (uptime ~%u s)\n",
    //            pit_ticks, pit_ticks / 100);

    pic_send_eoi(IRQ0);
}

/* =========================================================================
 * Initialisation
 * ========================================================================= */

/* Declared in isr.s / cpu.h */
extern void irq0(void);

void pit_init(uint32_t hz)
{
    /* Calculate divisor, clamp to valid range [1, 65535] */
    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor == 0)    divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Program PIT channel 0: mode 3 (square wave), binary counting */
    outb(PIT_CMD,      PIT_CMD_INIT);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));   /* low byte  */
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));     /* high byte */

    /* Register IRQ0 handler in IDT (vector 32 = PIC master offset 0x20) */
    idt_set_gate(32, (uint32_t)irq0, GDT_KERNEL_CODE, IDT_GATE_INT32);

    /* Unmask IRQ0 in the PIC */
    pic_enable_irq(IRQ0);
}

/* =========================================================================
 * Driver Layer Integration
 * ========================================================================= */

static char pit_read(int scnd_id)
{
    /* Return low byte of tick counter */
    (void)scnd_id;
    return (char)(pit_ticks & 0xFF);
}

static int pit_write(int scnd_id, char c)
{
    /* Writing to PIT does nothing */
    (void)scnd_id;
    (void)c;
    return 0;
}

int pit_register_driver(void)
{
    char_ops_t ops = {
        .read = pit_read,
        .write = pit_write
    };
    return register_char_device(1, &ops);
}
