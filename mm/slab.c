#include "slab.h"
#include "buddy.h"
#include "printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define KERNEL_VMA      0xC0000000U
#define PAGE_SIZE       4096
#define MAX_SLAB_CACHES 16          /* Maximum number of slab caches */
#define SLAB_MAGIC      0x534C4142  /* Magic number for slab identification (ASCII 'SLAB') */

/* =========================================================================
 * Slab Structures
 * ========================================================================= */

/* Free object list node (stored at beginning of free objects) */
typedef struct free_obj {
    struct free_obj *next;
} free_obj_t;

/* Slab metadata (stored at beginning of each slab page) */
typedef struct slab {
    uint32_t magic;           /* Magic number to identify slab pages */
    struct slab *next;        /* Next slab in cache list */
    uint16_t obj_size;        /* Size of each object */
    uint16_t num_objs;        /* Total objects in this slab */
    uint16_t free_count;      /* Number of free objects */
    free_obj_t *free_list;    /* Head of free object list */
} slab_t;

/* Slab cache (one per object size) */
typedef struct {
    uint16_t obj_size;        /* Size of objects in this cache */
    slab_t *partial;          /* Slabs with some free space */
    slab_t *full;             /* Fully allocated slabs */
} slab_cache_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static slab_cache_t slab_caches[MAX_SLAB_CACHES];
static int num_caches = 0;

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/* Round up to next power of 2 */
static uint16_t round_up_pow2(size_t size)
{
    if (size <= 8) return 8;
    
    uint16_t pow2 = 8;
    while (pow2 < size && pow2 < 2048) {
        pow2 <<= 1;
    }
    return pow2;
}

/* Get slab metadata from object address */
static inline slab_t *addr_to_slab(void *addr)
{
    /* Slab metadata is at the start of the page */
    uint32_t page_addr = (uint32_t)addr & ~(PAGE_SIZE - 1);
    return (slab_t *)page_addr;
}

/* Check if address is a slab page */
static inline int is_slab_page(void *addr)
{
    slab_t *slab = addr_to_slab(addr);
    return (slab->magic == SLAB_MAGIC);
}

/* Initialize a new slab page */
static slab_t *create_slab(uint16_t obj_size)
{
    /* Allocate a page from buddy allocator */
    void *page = page_alloc(PAGE_SIZE);
    if (!page) {
        return NULL;
    }
    
    /* Initialize slab metadata at start of page */
    slab_t *slab = (slab_t *)page;
    slab->magic = SLAB_MAGIC;
    slab->next = NULL;
    slab->obj_size = obj_size;
    
    /* Calculate how many objects fit in the page */
    /* Reserve space for slab_t header */
    uint32_t usable_space = PAGE_SIZE - sizeof(slab_t);
    slab->num_objs = usable_space / obj_size;
    slab->free_count = slab->num_objs;
    
    /* Build free list */
    uint8_t *obj_area = (uint8_t *)page + sizeof(slab_t);
    slab->free_list = NULL;
    
    for (int i = slab->num_objs - 1; i >= 0; i--) {
        free_obj_t *obj = (free_obj_t *)(obj_area + i * obj_size);
        obj->next = slab->free_list;
        slab->free_list = obj;
    }
    
    return slab;
}

/* Find or create cache for given object size */
static slab_cache_t *get_cache(uint16_t obj_size)
{
    /* Search for existing cache */
    for (int i = 0; i < num_caches; i++) {
        if (slab_caches[i].obj_size == obj_size) {
            return &slab_caches[i];
        }
    }
    
    /* No cache found, create new one */
    if (num_caches >= MAX_SLAB_CACHES) {
        return NULL;
    }
    
    slab_cache_t *cache = &slab_caches[num_caches++];
    cache->obj_size = obj_size;
    cache->partial = NULL;
    cache->full = NULL;
    
    return cache;
}

/* Move slab from partial to full list */
static void move_to_full(slab_cache_t *cache, slab_t *slab)
{
    /* Remove from partial list */
    if (cache->partial == slab) {
        cache->partial = slab->next;
    } else {
        slab_t *curr = cache->partial;
        while (curr && curr->next != slab) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = slab->next;
        }
    }
    
    /* Add to full list */
    slab->next = cache->full;
    cache->full = slab;
}

/* Move slab from full to partial list */
static void move_to_partial(slab_cache_t *cache, slab_t *slab)
{
    /* Remove from full list */
    if (cache->full == slab) {
        cache->full = slab->next;
    } else {
        slab_t *curr = cache->full;
        while (curr && curr->next != slab) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = slab->next;
        }
    }
    
    /* Add to partial list */
    slab->next = cache->partial;
    cache->partial = slab;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void slab_init(void)
{
    num_caches = 0;
    
    /* Create default caches for power-of-2 sizes */
    uint16_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    
    for (int i = 0; i < 9; i++) {
        add_slab(sizes[i]);
    }
}

/* =========================================================================
 * Public API - Allocation
 * ========================================================================= */

void *kalloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    
    /* Large allocations go directly to buddy allocator */
    if (size > 2048) {
        return page_alloc(size);
    }
    
    /* Round up to power of 2 */
    uint16_t obj_size = round_up_pow2(size);
    
    /* Get or create cache */
    slab_cache_t *cache = get_cache(obj_size);
    if (!cache) {
        return NULL;
    }
    
    /* Find slab with free space */
    slab_t *slab = cache->partial;
    
    /* No partial slab, create new one */
    if (!slab) {
        slab = create_slab(obj_size);
        if (!slab) {
            return NULL;
        }
        slab->next = cache->partial;
        cache->partial = slab;
    }
    
    /* Allocate object from free list */
    if (!slab->free_list) {
        return NULL;
    }
    
    free_obj_t *obj = slab->free_list;
    slab->free_list = obj->next;
    slab->free_count--;
    
    /* Move to full list if completely allocated */
    if (slab->free_count == 0) {
        move_to_full(cache, slab);
    }
    
    return (void *)obj;
}

/* =========================================================================
 * Public API - Deallocation
 * ========================================================================= */

void kfree(void *addr)
{
    if (!addr) {
        return;
    }
    
    /* Check if this is a slab page */
    if (!is_slab_page(addr)) {
        /* Not a slab allocation, free to buddy allocator */
        page_free(addr);
        return;
    }
    
    /* Get slab metadata */
    slab_t *slab = addr_to_slab(addr);
    uint16_t obj_size = slab->obj_size;
    
    /* Add object back to free list */
    free_obj_t *obj = (free_obj_t *)addr;
    obj->next = slab->free_list;
    slab->free_list = obj;
    
    int was_full = (slab->free_count == 0);
    slab->free_count++;
    
    /* Find the cache */
    slab_cache_t *cache = get_cache(obj_size);
    if (!cache) {
        return;
    }
    
    /* If slab was full, move to partial list */
    if (was_full) {
        move_to_partial(cache, slab);
    }
    
    /* If slab is now empty, consider returning it to buddy allocator */
    if (slab->free_count == slab->num_objs) {
        /* Only free if we have other slabs available */
        if (cache->partial != slab || slab->next != NULL) {
            /* Remove from partial list */
            if (cache->partial == slab) {
                cache->partial = slab->next;
            } else {
                slab_t *curr = cache->partial;
                while (curr && curr->next != slab) {
                    curr = curr->next;
                }
                if (curr) {
                    curr->next = slab->next;
                }
            }
            
            /* Return page to buddy allocator */
            page_free((void *)slab);
        }
    }
}

/* =========================================================================
 * Public API - Cache Management
 * ========================================================================= */

int add_slab(size_t obj_size)
{
    /* Round up to reasonable alignment */
    uint16_t size = round_up_pow2(obj_size);
    
    if (size > 2048) {
        return -1;
    }
    
    /* Check if cache already exists */
    for (int i = 0; i < num_caches; i++) {
        if (slab_caches[i].obj_size == size) {
            return 0;  /* Already exists */
        }
    }
    
    /* Create new cache */
    slab_cache_t *cache = get_cache(size);
    if (!cache) {
        return -1;
    }
    
    return 0;
}

/* =========================================================================
 * Public API - Statistics
 * ========================================================================= */

void slab_stats(uint32_t *total_allocated, uint32_t *total_free)
{
    uint32_t allocated = 0;
    uint32_t free = 0;
    
    for (int i = 0; i < num_caches; i++) {
        slab_cache_t *cache = &slab_caches[i];
        
        /* Count objects in partial slabs */
        for (slab_t *slab = cache->partial; slab != NULL; slab = slab->next) {
            uint32_t used = slab->num_objs - slab->free_count;
            allocated += used * slab->obj_size;
            free += slab->free_count * slab->obj_size;
        }
        
        /* Count objects in full slabs */
        for (slab_t *slab = cache->full; slab != NULL; slab = slab->next) {
            uint32_t used = slab->num_objs - slab->free_count;
            allocated += used * slab->obj_size;
            free += slab->free_count * slab->obj_size;
        }
    }
    
    if (total_allocated) *total_allocated = allocated;
    if (total_free) *total_free = free;
}
