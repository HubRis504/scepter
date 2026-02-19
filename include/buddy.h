#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Buddy Allocator - Physical Memory Manager
 *
 * Power-of-2 page allocator using the buddy system algorithm.
 * Manages physical memory starting from the end of the kernel image.
 * Returns virtual addresses (physical + KERNEL_VMA) for higher-half kernel.
 * ========================================================================= */

/* Page size: 4 KB */
#define PAGE_SIZE      4096
#define PAGE_SHIFT     12

/* Maximum order: 2^MAX_ORDER pages = 2^(MAX_ORDER + 12) bytes
 * MAX_ORDER = 10 → max allocation = 1024 pages = 4 MB */
#define MAX_ORDER      10

/* =========================================================================
 * Initialization
 * ========================================================================= */

/* Initialize the buddy allocator with available physical memory.
 * base_phys:  starting physical address (page-aligned)
 * total_kb:   total memory available in kilobytes
 * Must be called once during kernel initialization. */
void buddy_init(uint32_t base_phys, uint32_t total_kb);

/* =========================================================================
 * Allocation and Deallocation
 * ========================================================================= */

/* Allocate physical memory of at least 'size' bytes.
 * Returns a virtual address (physical + KERNEL_VMA) or NULL on failure.
 * Actual allocated size is rounded up to the nearest power-of-2 pages. */
void *page_alloc(size_t size);

/* Free physical memory previously allocated with page_alloc().
 * addr: virtual address returned by page_alloc() */
void page_free(void *addr);

/* =========================================================================
 * Statistics
 * ========================================================================= */

/* Get total number of pages managed by the allocator */
uint32_t buddy_total_pages(void);

/* Get current number of free pages */
uint32_t buddy_free_pages(void);

/* Get current number of allocated pages */
uint32_t buddy_used_pages(void);

#endif /* BUDDY_H */