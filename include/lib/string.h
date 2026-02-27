#ifndef LIB_STRING_H
#define LIB_STRING_H

#include <stddef.h>

/* =========================================================================
 * String Functions
 * ========================================================================= */

/**
 * Get the length of a string
 * @param s String to measure
 * @return Length of string (excluding null terminator)
 */
size_t strlen(const char *s);

/**
 * Compare two strings
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int strcmp(const char *s1, const char *s2);

/**
 * Compare first n characters of two strings
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * Copy a string
 * @param dst Destination buffer
 * @param src Source string
 * @return Pointer to destination
 */
char *strcpy(char *dst, const char *src);

/**
 * Copy at most n characters of a string
 * @param dst Destination buffer
 * @param src Source string
 * @param n Maximum number of characters to copy
 * @return Pointer to destination
 */
char *strncpy(char *dst, const char *src, size_t n);

/**
 * Concatenate two strings
 * @param dst Destination string (must have space)
 * @param src Source string to append
 * @return Pointer to destination
 */
char *strcat(char *dst, const char *src);

/**
 * Find first occurrence of character in string
 * @param s String to search
 * @param c Character to find
 * @return Pointer to first occurrence, or NULL if not found
 */
char *strchr(const char *s, int c);

/**
 * Find last occurrence of character in string
 * @param s String to search
 * @param c Character to find
 * @return Pointer to last occurrence, or NULL if not found
 */
char *strrchr(const char *s, int c);

/* =========================================================================
 * Memory Functions
 * ========================================================================= */

/**
 * Fill memory with a constant byte
 * @param s Memory area to fill
 * @param c Byte value to fill with
 * @param n Number of bytes to fill
 * @return Pointer to memory area
 */
void *memset(void *s, int c, size_t n);

/**
 * Copy memory area
 * @param dst Destination memory
 * @param src Source memory
 * @param n Number of bytes to copy
 * @return Pointer to destination
 */
void *memcpy(void *dst, const void *src, size_t n);

/**
 * Move memory area (handles overlapping regions)
 * @param dst Destination memory
 * @param src Source memory
 * @param n Number of bytes to move
 * @return Pointer to destination
 */
void *memmove(void *dst, const void *src, size_t n);

/**
 * Compare memory areas
 * @param s1 First memory area
 * @param s2 Second memory area
 * @param n Number of bytes to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* LIB_STRING_H */