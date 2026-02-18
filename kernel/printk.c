#include "printk.h"
#include "vga.h"
#include <stdint.h>

/* =========================================================================
 * Internal flags for format specifier parsing
 * ========================================================================= */

#define FL_LEFT   (1 << 0)   /* '-' : left-align                  */
#define FL_PLUS   (1 << 1)   /* '+' : always show sign            */
#define FL_SPACE  (1 << 2)   /* ' ' : space before positive       */
#define FL_ZERO   (1 << 3)   /* '0' : zero-pad                    */
#define FL_HASH   (1 << 4)   /* '#' : alternate form (0x prefix)  */
#define FL_UPPER  (1 << 5)   /* uppercase hex                     */
#define FL_SIGNED (1 << 6)   /* value is signed                   */
#define FL_LONG   (1 << 7)   /* 'l'  length modifier              */
#define FL_LLONG  (1 << 8)   /* 'll' length modifier              */

#define INT_BUF_SIZE 32      /* enough for 64-bit octal + sign    */

/* =========================================================================
 * Low-level output
 * ========================================================================= */

static void put_char(char c)
{
    vga_putchar(c);
}

static void put_str(const char *s, int len)
{
    for (int i = 0; i < len; i++)
        put_char(s[i]);
}

static void put_pad(char pad_char, int n)
{
    for (int i = 0; i < n; i++)
        put_char(pad_char);
}

/* =========================================================================
 * Integer → string conversion (no libc)
 * Returns length of string written into buf (NOT null-terminated).
 * buf must be at least INT_BUF_SIZE bytes; digits are written right-to-left
 * and the function returns a pointer to the start of the number within buf.
 * ========================================================================= */

static const char digits_lower[] = "0123456789abcdef";
static const char digits_upper[] = "0123456789ABCDEF";

static char *uint_to_str(unsigned long long val, int base, int upper,
                          char *buf_end, int *out_len)
{
    const char *digits = upper ? digits_upper : digits_lower;
    char *p = buf_end;
    if (val == 0) {
        *--p = '0';
    } else {
        while (val) {
            *--p = digits[val % (unsigned)base];
            val /= (unsigned)base;
        }
    }
    *out_len = (int)(buf_end - p);
    return p;
}

/* =========================================================================
 * Print an integer with full flag/width/precision support
 * ========================================================================= */

static void print_int(unsigned long long uval, int flags, int width,
                       int prec, int base)
{
    char buf[INT_BUF_SIZE];
    char *buf_end = buf + INT_BUF_SIZE;
    int  num_len  = 0;
    char sign     = 0;
    char prefix[2]; int prefix_len = 0;

    /* Handle sign for signed values */
    if (flags & FL_SIGNED) {
        long long sval = (long long)uval;
        if (sval < 0) {
            sign = '-';
            uval = (unsigned long long)(-sval);
        } else if (flags & FL_PLUS) {
            sign = '+';
        } else if (flags & FL_SPACE) {
            sign = ' ';
        }
    }

    /* Alternate form prefix: 0x / 0X for hex, 0 for octal */
    if (flags & FL_HASH) {
        if (base == 16) {
            prefix[0] = '0';
            prefix[1] = (flags & FL_UPPER) ? 'X' : 'x';
            prefix_len = 2;
        } else if (base == 8) {
            prefix[0] = '0';
            prefix_len = 1;
        }
    }

    /* Convert number */
    char *num_start = uint_to_str(uval, base, (flags & FL_UPPER) ? 1 : 0,
                                   buf_end, &num_len);

    /* Precision: minimum digit count (zero-pad the number itself) */
    if (prec < 0) prec = 1;
    int num_digits = (num_len > prec) ? num_len : prec;

    /* Total content width: sign + prefix + digits */
    int content = (sign ? 1 : 0) + prefix_len + num_digits;
    int pad     = (width > content) ? (width - content) : 0;

    char pad_char = ((flags & FL_ZERO) && !(flags & FL_LEFT)) ? '0' : ' ';

    /* Emit: [spaces] sign prefix [zeros] digits [spaces] */
    if (!(flags & FL_LEFT) && pad_char == ' ')
        put_pad(' ', pad);

    if (sign)        put_char(sign);
    if (prefix_len)  put_str(prefix, prefix_len);

    if (!(flags & FL_LEFT) && pad_char == '0')
        put_pad('0', pad);

    /* Leading zeros for precision */
    put_pad('0', num_digits - num_len);
    put_str(num_start, num_len);

    if (flags & FL_LEFT)
        put_pad(' ', pad);
}

/* =========================================================================
 * Print a string with width/precision support
 * ========================================================================= */

static void print_str(const char *s, int flags, int width, int prec)
{
    if (!s) s = "(null)";

    /* Compute length, capped by precision */
    int len = 0;
    while (s[len]) len++;
    if (prec >= 0 && len > prec) len = prec;

    int pad = (width > len) ? (width - len) : 0;

    if (!(flags & FL_LEFT)) put_pad(' ', pad);
    put_str(s, len);
    if (flags & FL_LEFT)    put_pad(' ', pad);
}

/* =========================================================================
 * Core variadic formatter
 * ========================================================================= */

void vprintk(const char *fmt, va_list args)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put_char(*fmt);
            continue;
        }

        fmt++;  /* skip '%' */
        if (!*fmt) break;

        /* ---- Parse flags ---- */
        int flags = 0;
        for (;;) {
            if      (*fmt == '-') { flags |= FL_LEFT;  fmt++; }
            else if (*fmt == '+') { flags |= FL_PLUS;  fmt++; }
            else if (*fmt == ' ') { flags |= FL_SPACE; fmt++; }
            else if (*fmt == '0') { flags |= FL_ZERO;  fmt++; }
            else if (*fmt == '#') { flags |= FL_HASH;  fmt++; }
            else break;
        }

        /* ---- Parse width ---- */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(args, int);
            if (width < 0) { flags |= FL_LEFT; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        /* ---- Parse precision ---- */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(args, int);
                if (prec < 0) prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }

        /* ---- Parse length modifier ---- */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { flags |= FL_LLONG; fmt++; }
            else              { flags |= FL_LONG;        }
        } else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') fmt++;  /* hh – treat as int (promoted) */
        }

        /* ---- Dispatch on specifier ---- */
        switch (*fmt) {
        case 'c':
            {
                char c = (char)va_arg(args, int);
                int pad = (width > 1) ? (width - 1) : 0;
                if (!(flags & FL_LEFT)) put_pad(' ', pad);
                put_char(c);
                if (flags & FL_LEFT)    put_pad(' ', pad);
            }
            break;

        case 's':
            {
                const char *s = va_arg(args, const char *);
                print_str(s, flags, width, prec);
            }
            break;

        case 'd':
        case 'i':
            {
                long long val;
                if      (flags & FL_LLONG) val = va_arg(args, long long);
                else if (flags & FL_LONG)  val = va_arg(args, long);
                else                       val = va_arg(args, int);
                print_int((unsigned long long)val,
                          flags | FL_SIGNED, width, prec, 10);
            }
            break;

        case 'u':
            {
                unsigned long long val;
                if      (flags & FL_LLONG) val = va_arg(args, unsigned long long);
                else if (flags & FL_LONG)  val = va_arg(args, unsigned long);
                else                       val = va_arg(args, unsigned int);
                print_int(val, flags, width, prec, 10);
            }
            break;

        case 'x':
            {
                unsigned long long val;
                if      (flags & FL_LLONG) val = va_arg(args, unsigned long long);
                else if (flags & FL_LONG)  val = va_arg(args, unsigned long);
                else                       val = va_arg(args, unsigned int);
                print_int(val, flags, width, prec, 16);
            }
            break;

        case 'X':
            {
                unsigned long long val;
                if      (flags & FL_LLONG) val = va_arg(args, unsigned long long);
                else if (flags & FL_LONG)  val = va_arg(args, unsigned long);
                else                       val = va_arg(args, unsigned int);
                print_int(val, flags | FL_UPPER, width, prec, 16);
            }
            break;

        case 'o':
            {
                unsigned long long val;
                if      (flags & FL_LLONG) val = va_arg(args, unsigned long long);
                else if (flags & FL_LONG)  val = va_arg(args, unsigned long);
                else                       val = va_arg(args, unsigned int);
                print_int(val, flags, width, prec, 8);
            }
            break;

        case 'p':
            {
                uintptr_t val = (uintptr_t)va_arg(args, void *);
                /* Always print as 0x + lowercase hex, field width 10 (32-bit) */
                flags |= FL_HASH;
                if (width == 0) width = 10;
                print_int((unsigned long long)val, flags, width, prec, 16);
            }
            break;

        case '%':
            put_char('%');
            break;

        default:
            /* Unknown specifier: emit literally */
            put_char('%');
            put_char(*fmt);
            break;
        }
    }
}

/* =========================================================================
 * Public entry point
 * ========================================================================= */

void printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}
