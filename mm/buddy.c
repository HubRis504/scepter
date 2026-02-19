#include "buddy.h"
#include "printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define KERNEL_VMA  0xC0000000U

/* =========================================================================
 * Free Block Structure
 *
 * Each free block stores a linked list node at its beginning.
 * When allocated, this space is available to the user.
 * ========================================================================= */

typedef struct free_block {
    struct free_block *next;
} free_block_t;

/* =========================================================================
 * Allocator State
 * ========================================================================= */

typedef struct {
    free_block_t *free_lists[MAX_ORDER + 1];  /* One free list per order */
    uint32_t total_pages;                      /* Total pages managed */
    uint32_t free_pages;                       /* Currently free pages */
    uint32_t base_phys;                        /* Starting physical address */
} buddy_allocator_t;

static buddy_allocator_t buddy;

/* Track allocation order for each page (simple bitmap approach) */
#define MAX_TRACKED_PAGES 262144  /* 1GB / 4KB */
static uint8_t alloc_order[MAX_TRACKED_PAGES];

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/* Convert physical address to virtual address */
static inline void *phys_to_virt(uint32_t phys)
{
    return (void *)(phys + KERNEL_VMA);
}

/* Convert virtual address to physical address */
static inline uint32_t virt_to_phys(void *virt)
{
    return (uint32_t)virt - KERNEL_VMA;
}

/* Calculate the buddy address for a given block */
static inline uint32_t buddy_addr(uint32_t addr, uint32_t order)
{
    uint32_t size = PAGE_SIZE << order;
    return addr ^ size;
}

/* Check if an address is aligned to the given order */
static inline int is_aligned(uint32_t addr, uint32_t order)
{
    uint32_t size = PAGE_SIZE << order;
    return (addr & (size - 1)) == 0;
}

/* Calculate the order needed for a given size in bytes */
static uint32_t order_from_size(size_t size)
{
    /* Round up to page size */
    uint32_t pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    
    /* Special case: 0 or 1 page needs order 0 */
    if (pages <= 1) {
        return 0;
    }
    
    /* Find the order (power of 2 pages) */
    uint32_t order = 0;
    uint32_t p = 1;
    while (p < pages && order < MAX_ORDER) {
        p <<= 1;
        order++;
    }
    
    return order;
}

/* Remove a block from a free list */
static void remove_from_list(free_block_t **list, free_block_t *block)
{
    if (*list == block) {
        *list = block->next;
    } else {
        free_block_t *curr = *list;
        while (curr && curr->next != block) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = block->next;
        }
    }
}

/* Add a block to the front of a free list */
static void add_to_list(free_block_t **list, free_block_t *block)
{
    block->next = *list;
    *list = block;
}

/* Split a block of given order into two buddies of order-1 */
static void split_block(uint32_t order)
{
    if (order == 0 || order > MAX_ORDER) return;
    
    /* Get a block from the higher order list */
    free_block_t *block = buddy.free_lists[order];
    if (!block) return;
    
    /* Remove from current list */
    buddy.free_lists[order] = block->next;
    
    /* Calculate buddy addresses */
    uint32_t phys = virt_to_phys(block);
    uint32_t size = PAGE_SIZE << (order - 1);
    
    free_block_t *block1 = block;
    free_block_t *block2 = phys_to_virt(phys + size);
    
    /* Add both buddies to the lower order list */
    add_to_list(&buddy.free_lists[order - 1], block1);
    add_to_list(&buddy.free_lists[order - 1], block2);
}

/* Try to coalesce a block with its buddy */
static uint32_t coalesce(uint32_t phys, uint32_t order)
{
    while (order < MAX_ORDER) {
        uint32_t buddy_phys = buddy_addr(phys, order);
        
        /* Check if buddy exists and is free */
        free_block_t *curr = buddy.free_lists[order];
        free_block_t *buddy_block = NULL;
        
        while (curr) {
            if (virt_to_phys(curr) == buddy_phys) {
                buddy_block = curr;
                break;
            }
            curr = curr->next;
        }
        
        if (!buddy_block) {
            /* Buddy not free, stop coalescing */
            break;
        }
        
        /* Remove buddy from free list */
        remove_from_list(&buddy.free_lists[order], buddy_block);
        
        /* Coalesce: use the lower address of the two */
        if (buddy_phys < phys) {
            phys = buddy_phys;
        }
        
        order++;
    }
    
    return order;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void buddy_init(uint32_t base_phys, uint32_t total_kb)
{
    /* Initialize free lists */
    for (int i = 0; i <= MAX_ORDER; i++) {
        buddy.free_lists[i] = NULL;
    }
    
    buddy.base_phys = base_phys;
    
    /* Calculate total pages available (convert KB to pages) */
    uint32_t total_bytes = total_kb * 1024;
    buddy.total_pages = total_bytes >> PAGE_SHIFT;
    buddy.free_pages = 0;
    
    /* Silent initialization - details already printed by kernel.c */
    
    /* Add free memory in largest possible blocks */
    uint32_t curr_phys = base_phys;
    uint32_t end_phys = base_phys + (buddy.total_pages << PAGE_SHIFT);
    
    while (curr_phys < end_phys) {
        /* Find largest order that fits */
        uint32_t order = MAX_ORDER;
        uint32_t size = PAGE_SIZE << order;
        
        while (order > 0) {
            size = PAGE_SIZE << order;
            
            /* Check if block fits and is aligned */
            if (is_aligned(curr_phys, order) && (curr_phys + size) <= end_phys) {
                break;
            }
            order--;
        }
        
        /* Add block to free list */
        free_block_t *block = phys_to_virt(curr_phys);
        add_to_list(&buddy.free_lists[order], block);
        
        buddy.free_pages += (1 << order);
        curr_phys += size;
    }
}

/* =========================================================================
 * Public API - Allocation
 * ========================================================================= */

void *page_alloc(size_t size)
{
    if (size == 0) return NULL;
    
    uint32_t order = order_from_size(size);
    
    if (order > MAX_ORDER) {
        return NULL;
    }
    
    /* Find a free block of sufficient size */
    uint32_t curr_order = order;
    while (curr_order <= MAX_ORDER && !buddy.free_lists[curr_order]) {
        curr_order++;
    }
    
    if (curr_order > MAX_ORDER) {
        return NULL;
    }
    
    /* Split larger blocks if necessary */
    while (curr_order > order) {
        split_block(curr_order);
        curr_order--;
    }
    
    /* Allocate block from the free list */
    free_block_t *block = buddy.free_lists[order];
    if (!block) {
        return NULL;
    }
    
    buddy.free_lists[order] = block->next;
    buddy.free_pages -= (1 << order);
    
    /* Track allocation order */
    uint32_t phys = virt_to_phys(block);
    uint32_t page_idx = (phys - buddy.base_phys) >> PAGE_SHIFT;
    if (page_idx < MAX_TRACKED_PAGES) {
        alloc_order[page_idx] = order;
    }
    
    return (void *)block;
}

/* =========================================================================
 * Public API - Deallocation
 * ========================================================================= */

void page_free(void *addr)
{
    if (!addr) return;
    
    uint32_t phys = virt_to_phys(addr);
    
    /* Validate address is within managed range */
    if (phys < buddy.base_phys) {
        return;
    }
    
    /* Get the original allocation order */
    uint32_t page_idx = (phys - buddy.base_phys) >> PAGE_SHIFT;
    uint32_t orig_order = 0;
    if (page_idx < MAX_TRACKED_PAGES) {
        orig_order = alloc_order[page_idx];
        alloc_order[page_idx] = 0;  /* Clear tracking */
    }
    
    /* Return the originally allocated pages to free count */
    buddy.free_pages += (1 << orig_order);
    
    /* Try to coalesce with buddy (this doesn't change free_pages count,
     * just reorganizes blocks into larger ones) */
    uint32_t order = coalesce(phys, orig_order);
    
    /* Add to appropriate free list */
    free_block_t *block = phys_to_virt(phys);
    add_to_list(&buddy.free_lists[order], block);
}

/* =========================================================================
 * Public API - Statistics
 * ========================================================================= */

uint32_t buddy_total_pages(void)
{
    return buddy.total_pages;
}

uint32_t buddy_free_pages(void)
{
    return buddy.free_pages;
}

uint32_t buddy_used_pages(void)
{
    return buddy.total_pages - buddy.free_pages;
}