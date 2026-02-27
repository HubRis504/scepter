#include "driver/char/kbd.h"
#include "driver/driver.h"
#include "driver/pic.h"
#include "kernel/cpu.h"
#include "asm.h"

/* =========================================================================
 * PS/2 Keyboard Constants
 * ========================================================================= */

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

#define KBD_BUFFER_SIZE  128

/* Scancodes for special keys */
#define SC_LSHIFT        0x2A
#define SC_RSHIFT        0x36
#define SC_LSHIFT_REL    0xAA
#define SC_RSHIFT_REL    0xB6
#define SC_CAPSLOCK      0x3A
#define SC_ENTER         0x1C
#define SC_BACKSPACE     0x0E
#define SC_TAB           0x0F
#define SC_ESC           0x01

/* =========================================================================
 * Keyboard State
 * ========================================================================= */

static struct {
    char buffer[KBD_BUFFER_SIZE];
    int read_pos;
    int write_pos;
    int count;
    uint8_t shift_pressed;
    uint8_t caps_lock;
} kbd_state = {0};

/* =========================================================================
 * Scancode to ASCII Translation Tables
 * ========================================================================= */

/* Normal (unshifted) scancode to ASCII */
static const char scancode_to_ascii[] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',   /* 00-07 */
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',  /* 08-0F */
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',   /* 10-17 */
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',   /* 18-1F */
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 20-27 */
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',   /* 28-2F */
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',   /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,     /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    '7',   /* 40-47 */
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',   /* 48-4F */
    '2',  '3',  '0',  '.',  0,    0,    0,    0      /* 50-57 */
};

/* Shifted scancode to ASCII */
static const char scancode_to_ascii_shift[] = {
    0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',   /* 00-07 */
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',  /* 08-0F */
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',   /* 10-17 */
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',   /* 18-1F */
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',   /* 20-27 */
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',   /* 28-2F */
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',   /* 30-37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,     /* 38-3F */
    0,    0,    0,    0,    0,    0,    0,    '7',   /* 40-47 */
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',   /* 48-4F */
    '2',  '3',  '0',  '.',  0,    0,    0,    0      /* 50-57 */
};

/* =========================================================================
 * Circular Buffer Operations
 * ========================================================================= */

static void kbd_buffer_push(char c)
{
    if (kbd_state.count < KBD_BUFFER_SIZE) {
        kbd_state.buffer[kbd_state.write_pos] = c;
        kbd_state.write_pos = (kbd_state.write_pos + 1) % KBD_BUFFER_SIZE;
        kbd_state.count++;
    }
    /* If buffer full, character is dropped */
}

static char kbd_buffer_pop(void)
{
    if (kbd_state.count > 0) {
        char c = kbd_state.buffer[kbd_state.read_pos];
        kbd_state.read_pos = (kbd_state.read_pos + 1) % KBD_BUFFER_SIZE;
        kbd_state.count--;
        return c;
    }
    return 0;  /* Buffer empty */
}

/* =========================================================================
 * IRQ1 Handler - Keyboard Interrupt
 * ========================================================================= */

void kbd_isr(void)
{
    uint8_t scancode = inb(KBD_DATA_PORT);
    
    /* Handle modifier keys */
    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        kbd_state.shift_pressed = 1;
        pic_send_eoi(IRQ1);
        return;
    }
    
    if (scancode == SC_LSHIFT_REL || scancode == SC_RSHIFT_REL) {
        kbd_state.shift_pressed = 0;
        pic_send_eoi(IRQ1);
        return;
    }
    
    if (scancode == SC_CAPSLOCK) {
        kbd_state.caps_lock = !kbd_state.caps_lock;
        pic_send_eoi(IRQ1);
        return;
    }
    
    /* Ignore break codes (key release) */
    if (scancode & 0x80) {
        pic_send_eoi(IRQ1);
        return;
    }
    
    /* Convert scancode to ASCII */
    char ascii = 0;
    
    if (scancode < sizeof(scancode_to_ascii)) {
        if (kbd_state.shift_pressed) {
            ascii = scancode_to_ascii_shift[scancode];
        } else {
            ascii = scancode_to_ascii[scancode];
        }
        
        /* Apply Caps Lock to letters */
        if (kbd_state.caps_lock && ascii >= 'a' && ascii <= 'z') {
            ascii = ascii - 'a' + 'A';
        } else if (kbd_state.caps_lock && ascii >= 'A' && ascii <= 'Z') {
            ascii = ascii - 'A' + 'a';
        }
    }
    
    /* Push to buffer if valid ASCII */
    if (ascii != 0) {
        kbd_buffer_push(ascii);
    }
    
    pic_send_eoi(IRQ1);
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

extern void irq1(void);  /* Defined in isr.s */

void kbd_init(void)
{
    /* Initialize state */
    kbd_state.read_pos = 0;
    kbd_state.write_pos = 0;
    kbd_state.count = 0;
    kbd_state.shift_pressed = 0;
    kbd_state.caps_lock = 0;
    
    /* Register IRQ1 handler in IDT (vector 33 = PIC master offset + 1) */
    idt_set_gate(33, (uint32_t)irq1, GDT_KERNEL_CODE, IDT_GATE_INT32);
    
    /* Unmask IRQ1 in PIC */
    pic_enable_irq(IRQ1);
}

/* =========================================================================
 * Driver Layer Integration
 * ========================================================================= */

static char kbd_read(int scnd_id)
{
    /* Validate scnd_id must be 0 */
    if (scnd_id != 0) {
        return 0;
    }
    
    /* Return next character from buffer (0 if empty) */
    return kbd_buffer_pop();
}

static int kbd_write(int scnd_id, char c)
{
    /* Keyboard doesn't support writing */
    (void)scnd_id;
    (void)c;
    return -1;
}

static int kbd_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    /* Keyboard doesn't support ioctl */
    (void)prim_id;
    (void)scnd_id;
    (void)command;
    return -1;
}

int kbd_register_driver(void)
{
    char_ops_t ops = {
        .read = kbd_read,
        .write = kbd_write,
        .ioctl = kbd_ioctl
    };
    return register_char_device(3, &ops);
}