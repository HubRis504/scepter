#ifndef SLAB_H
#define SLAB_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Slab Allocator - Efficient small object allocation
 *
 * Built on top of the buddy allocator for page-level memory management.
 * Provides fast allocation/deallocation of small, fixed-size objects.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Initialize the slab allocator with default caches.
 * Must be called after buddy allocator is initialized.
 */
void slab_init(void);

/**
 * Allocate memory of specified size.
 * Rounds up to nearest power of 2 and uses appropriate slab cache.
 * Sizes > 2KB are allocated directly from buddy allocator.
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *kalloc(size_t size);

/**
 * Free previously allocated memory.
 * Automatically detects if memory came from slab or buddy allocator.
 *
 * @param addr Pointer to memory to free
 */
void kfree(void *addr);

/**
 * Add a new slab cache for a specific object size.
 * Useful for frequently allocated structures (e.g., task_struct).
 *
 * @param obj_size Size of objects in this cache (will be rounded up)
 * @return 0 on success, -1 on failure
 */
int add_slab(size_t obj_size);

/**
 * Get slab allocator statistics.
 * 
 * @param total_allocated Total bytes allocated (output)
 * @param total_free Total bytes free in slabs (output)
 */
void slab_stats(uint32_t *total_allocated, uint32_t *total_free);

#endif /* SLAB_H */