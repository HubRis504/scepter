#include "driver.h"
#include "slab.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Device Structure (Internal)
 * ========================================================================= */

typedef struct device {
    int prim_id;
    dev_type_t type;
    union {
        char_ops_t char_ops;
        block_ops_t block_ops;
    } ops;
    struct device *next;
} device_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static device_t *device_list = NULL;

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

/* Find a device by primary ID */
static device_t *find_device(int prim_id)
{
    device_t *dev = device_list;
    while (dev) {
        if (dev->prim_id == prim_id) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void driver_init(void)
{
    device_list = NULL;
}

/* =========================================================================
 * Public API - Device Registration
 * ========================================================================= */

int register_char_device(int prim_id, char_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255) {
        return -1;
    }
    
    /* Check if device already registered */
    if (find_device(prim_id)) {
        return -1;
    }
    
    /* Allocate new device structure */
    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev) {
        return -1;
    }
    
    /* Initialize device */
    dev->prim_id = prim_id;
    dev->type = DEV_CHAR;
    dev->ops.char_ops = *ops;
    
    /* Add to front of list */
    dev->next = device_list;
    device_list = dev;
    
    return 0;
}

int register_block_device(int prim_id, block_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255) {
        return -1;
    }
    
    /* Check if device already registered */
    if (find_device(prim_id)) {
        return -1;
    }
    
    /* Allocate new device structure */
    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev) {
        return -1;
    }
    
    /* Initialize device */
    dev->prim_id = prim_id;
    dev->type = DEV_BLOCK;
    dev->ops.block_ops = *ops;
    
    /* Add to front of list */
    dev->next = device_list;
    device_list = dev;
    
    return 0;
}

/* =========================================================================
 * Public API - Character Device Operations
 * ========================================================================= */

char cread(int prim_id, int scnd_id)
{
    device_t *dev = find_device(prim_id);
    if (!dev || dev->type != DEV_CHAR) {
        return 0;
    }
    
    if (!dev->ops.char_ops.read) {
        return 0;
    }
    
    return dev->ops.char_ops.read(scnd_id);
}

int cwrite(int prim_id, int scnd_id, char c)
{
    device_t *dev = find_device(prim_id);
    if (!dev || dev->type != DEV_CHAR) {
        return -1;
    }
    
    if (!dev->ops.char_ops.write) {
        return -1;
    }
    
    return dev->ops.char_ops.write(scnd_id, c);
}

/* =========================================================================
 * Public API - Block Device Operations
 * ========================================================================= */

int bread(int prim_id, int scnd_id, void *buf, size_t count)
{
    device_t *dev = find_device(prim_id);
    if (!dev || dev->type != DEV_BLOCK) {
        return -1;
    }
    
    if (!dev->ops.block_ops.read) {
        return -1;
    }
    
    return dev->ops.block_ops.read(scnd_id, buf, count);
}

int bwrite(int prim_id, int scnd_id, const void *buf, size_t count)
{
    device_t *dev = find_device(prim_id);
    if (!dev || dev->type != DEV_BLOCK) {
        return -1;
    }
    
    if (!dev->ops.block_ops.write) {
        return -1;
    }
    
    return dev->ops.block_ops.write(scnd_id, buf, count);
}