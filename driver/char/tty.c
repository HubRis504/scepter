#include "driver/char/tty.h"
#include "driver/char/vga.h"
#include "driver/driver.h"
#include <stdint.h>

/* =========================================================================
 * TTY Constants
 * ========================================================================= */

#define TTY_WIDTH   80
#define TTY_HEIGHT  25
#define TAB_WIDTH   8

/* Escape sequence parser states */
typedef enum {
    TTY_STATE_NORMAL,      /* Normal character output */
    TTY_STATE_ESC,         /* Received ESC (0x1B) */
    TTY_STATE_CSI,         /* Received ESC[ - parsing CSI sequence */
} tty_state_t;

/* =========================================================================
 * TTY State
 * ========================================================================= */

static struct {
    uint8_t col;
    uint8_t row;
    tty_color_t fg;
    tty_color_t bg;
    tty_state_t state;
    int params[8];         /* CSI parameters */
    int param_count;
    int current_param;
    uint8_t bold;
} tty;

/* =========================================================================
 * VGA Backend Functions
 * ========================================================================= */

static void tty_write_cell(uint8_t col, uint8_t row, char c, uint8_t color)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xC00B8000;
    vga[row * TTY_WIDTH + col] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void tty_update_hw_cursor(void)
{
    vga_set_cursor(tty.col, tty.row);
}

static uint8_t tty_make_color(tty_color_t fg, tty_color_t bg)
{
    return (uint8_t)(fg | (bg << 4));
}

/* =========================================================================
 * TTY Operations
 * ========================================================================= */

void tty_init(void)
{
    tty.col = 0;
    tty.row = 0;
    tty.fg = TTY_WHITE;
    tty.bg = TTY_BLACK;
    tty.state = TTY_STATE_NORMAL;
    tty.param_count = 0;
    tty.current_param = 0;
    tty.bold = 0;
}

void tty_clear(void)
{
    uint8_t color = tty_make_color(tty.fg, tty.bg);
    for (int row = 0; row < TTY_HEIGHT; row++) {
        for (int col = 0; col < TTY_WIDTH; col++) {
            tty_write_cell(col, row, ' ', color);
        }
    }
    tty.col = 0;
    tty.row = 0;
    tty_update_hw_cursor();
}

void tty_set_color(tty_color_t fg, tty_color_t bg)
{
    tty.fg = fg;
    tty.bg = bg;
}

void tty_get_cursor(uint8_t *col, uint8_t *row)
{
    if (col) *col = tty.col;
    if (row) *row = tty.row;
}

void tty_set_cursor(uint8_t col, uint8_t row)
{
    if (col >= TTY_WIDTH) col = TTY_WIDTH - 1;
    if (row >= TTY_HEIGHT) row = TTY_HEIGHT - 1;
    tty.col = col;
    tty.row = row;
    tty_update_hw_cursor();
}

/* Scroll screen up by one line */
static void tty_scroll(void)
{
    uint8_t color = tty_make_color(tty.fg, tty.bg);
    volatile uint16_t *vga = (volatile uint16_t *)0xC00B8000;
    
    /* Move all rows up */
    for (int row = 1; row < TTY_HEIGHT; row++) {
        for (int col = 0; col < TTY_WIDTH; col++) {
            vga[(row - 1) * TTY_WIDTH + col] = vga[row * TTY_WIDTH + col];
        }
    }
    
    /* Clear last row */
    for (int col = 0; col < TTY_WIDTH; col++) {
        tty_write_cell(col, TTY_HEIGHT - 1, ' ', color);
    }
    
    if (tty.row > 0) tty.row--;
}

/* Handle newline */
static void tty_newline(void)
{
    tty.col = 0;
    tty.row++;
    if (tty.row >= TTY_HEIGHT) {
        tty_scroll();
    }
}

/* =========================================================================
 * ANSI Escape Sequence Parser
 * ========================================================================= */

/* Execute CSI sequence */
static void tty_execute_csi(char command)
{
    int n, m;
    
    switch (command) {
    case 'A':  /* Cursor up */
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        if (tty.row >= n) tty.row -= n;
        else tty.row = 0;
        break;
        
    case 'B':  /* Cursor down */
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        tty.row += n;
        if (tty.row >= TTY_HEIGHT) tty.row = TTY_HEIGHT - 1;
        break;
        
    case 'C':  /* Cursor forward */
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        tty.col += n;
        if (tty.col >= TTY_WIDTH) tty.col = TTY_WIDTH - 1;
        break;
        
    case 'D':  /* Cursor backward */
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        if (tty.col >= n) tty.col -= n;
        else tty.col = 0;
        break;
        
    case 'H':  /* Cursor position (row;col) */
    case 'f':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] - 1 : 0;
        m = (tty.param_count > 1 && tty.params[1] > 0) ? tty.params[1] - 1 : 0;
        tty_set_cursor(m, n);
        return;  /* Already updated cursor */
        
    case 'J':  /* Erase display */
        n = (tty.param_count > 0) ? tty.params[0] : 0;
        if (n == 2) {  /* Clear entire screen */
            tty_clear();
            return;
        }
        break;
        
    case 'K':  /* Erase line */
        {
            uint8_t color = tty_make_color(tty.fg, tty.bg);
            for (int col = tty.col; col < TTY_WIDTH; col++) {
                tty_write_cell(col, tty.row, ' ', color);
            }
        }
        break;
        
    case 'm':  /* Set graphics mode */
        for (int i = 0; i < tty.param_count; i++) {
            int param = tty.params[i];
            if (param == 0) {  /* Reset */
                tty.fg = TTY_WHITE;
                tty.bg = TTY_BLACK;
                tty.bold = 0;
            } else if (param == 1) {  /* Bold */
                tty.bold = 1;
            } else if (param >= 30 && param <= 37) {  /* Foreground color */
                /* ANSI to VGA color mapping: ANSI order is Black,Red,Green,Yellow,Blue,Magenta,Cyan,White
                   VGA order is Black,Blue,Green,Cyan,Red,Magenta,Yellow,White */
                static const uint8_t ansi_to_vga[] = {0, 4, 2, 6, 1, 5, 3, 7};
                int ansi_idx = param - 30;
                tty.fg = (tty_color_t)(ansi_to_vga[ansi_idx] + (tty.bold ? 8 : 0));
            } else if (param >= 40 && param <= 47) {  /* Background color */
                static const uint8_t ansi_to_vga[] = {0, 4, 2, 6, 1, 5, 3, 7};
                int ansi_idx = param - 40;
                tty.bg = (tty_color_t)ansi_to_vga[ansi_idx];
            }
        }
        break;
    }
    
    tty_update_hw_cursor();
}

/* =========================================================================
 * Character Output
 * ========================================================================= */

void tty_putchar(char c)
{
    uint8_t color = tty_make_color(tty.fg, tty.bg);
    
    switch (tty.state) {
    case TTY_STATE_NORMAL:
        /* Check for escape sequence start */
        if (c == '\033') {  /* ESC */
            tty.state = TTY_STATE_ESC;
            return;
        }
        
        /* Handle control characters */
        switch (c) {
        case '\n':  /* Line feed */
            tty_newline();
            break;
            
        case '\r':  /* Carriage return */
            tty.col = 0;
            break;
            
        case '\b':  /* Backspace */
            if (tty.col > 0) {
                tty.col--;
                tty_write_cell(tty.col, tty.row, ' ', color);
            }
            break;
            
        case '\t':  /* Tab */
            {
                int next_tab = ((tty.col / TAB_WIDTH) + 1) * TAB_WIDTH;
                if (next_tab >= TTY_WIDTH) {
                    tty_newline();
                } else {
                    while (tty.col < next_tab) {
                        tty_write_cell(tty.col, tty.row, ' ', color);
                        tty.col++;
                    }
                }
            }
            break;
            
        case '\a':  /* Bell - ignore for now */
            break;
            
        default:  /* Regular character */
            if (c >= 32 && c <= 126) {  /* Printable ASCII */
                tty_write_cell(tty.col, tty.row, c, color);
                tty.col++;
                if (tty.col >= TTY_WIDTH) {
                    tty_newline();
                }
            }
            break;
        }
        break;
        
    case TTY_STATE_ESC:
        if (c == '[') {
            /* Start of CSI sequence */
            tty.state = TTY_STATE_CSI;
            tty.param_count = 0;
            tty.current_param = 0;
            for (int i = 0; i < 8; i++) tty.params[i] = 0;
        } else {
            /* Unknown escape sequence, return to normal */
            tty.state = TTY_STATE_NORMAL;
        }
        break;
        
    case TTY_STATE_CSI:
        if (c >= '0' && c <= '9') {
            /* Accumulate parameter digit */
            tty.current_param = tty.current_param * 10 + (c - '0');
        } else if (c == ';') {
            /* Parameter separator */
            if (tty.param_count < 8) {
                tty.params[tty.param_count++] = tty.current_param;
                tty.current_param = 0;
            }
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            /* Command letter - save last parameter and execute */
            if (tty.param_count < 8) {
                tty.params[tty.param_count++] = tty.current_param;
            }
            tty_execute_csi(c);
            tty.state = TTY_STATE_NORMAL;
        } else {
            /* Invalid character, abort sequence */
            tty.state = TTY_STATE_NORMAL;
        }
        break;
    }
    
    tty_update_hw_cursor();
}

void tty_puts(const char *str)
{
    while (*str) {
        tty_putchar(*str++);
    }
}

/* =========================================================================
 * Driver Layer Integration
 * ========================================================================= */

/* ioctl commands */
#define TTY_IOCTL_CLEAR  0x1

static char tty_read(int scnd_id)
{
    /* Forward read to keyboard driver (device 3) */
    extern char cread(int, int);
    (void)scnd_id;
    return cread(3, 0);
}

static int tty_write(int scnd_id, char c)
{
    (void)scnd_id;
    tty_putchar(c);
    return 0;
}

static int tty_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    (void)prim_id;
    (void)scnd_id;
    
    switch (command) {
        case TTY_IOCTL_CLEAR:
            tty_clear();
            return 0;
        default:
            return -1;  /* Unknown command */
    }
}

int tty_register_driver(void)
{
    char_ops_t ops = {
        .read = tty_read,
        .write = tty_write,
        .ioctl = tty_ioctl
    };
    return register_char_device(2, &ops);
}
