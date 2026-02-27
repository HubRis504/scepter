#include "lib/string.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * String Functions
 * ========================================================================= */

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *ret = dst;
    while (n > 0) {
        if (*src) {
            *dst++ = *src++;
        } else {
            *dst++ = '\0';
        }
        n--;
    }
    return ret;
}

char *strcat(char *dst, const char *src)
{
    char *ret = dst;
    /* Find end of destination string */
    while (*dst) {
        dst++;
    }
    /* Append source string */
    while ((*dst++ = *src++));
    return ret;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    /* Check if searching for null terminator */
    if ((char)c == '\0') {
        return (char *)s;
    }
    return NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    /* Check if searching for null terminator */
    if ((char)c == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

/* =========================================================================
 * Memory Functions
 * ========================================================================= */

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char value = (unsigned char)c;
    
    while (n > 0) {
        *p++ = value;
        n--;
    }
    
    return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    
    while (n > 0) {
        *d++ = *s++;
        n--;
    }
    
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    
    /* Check for overlap */
    if (d < s) {
        /* Copy forwards */
        while (n > 0) {
            *d++ = *s++;
            n--;
        }
    } else if (d > s) {
        /* Copy backwards to handle overlap */
        d += n;
        s += n;
        while (n > 0) {
            *--d = *--s;
            n--;
        }
    }
    /* If d == s, no copy needed */
    
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    
    while (n > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
        n--;
    }
    
    return 0;
}