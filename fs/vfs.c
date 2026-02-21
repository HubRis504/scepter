#include "fs.h"
#include "slab.h"
#include "printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * String utilities (minimal, no libc)
 * ========================================================================= */

static size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static char *strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* =========================================================================
 * Internal Structures
 * ========================================================================= */

/* File handle (linked list node) */
typedef struct file_handle {
    int fd;                        /* File descriptor number */
    int fs_id;                     /* Filesystem driver ID */
    void *fs_private;              /* Filesystem-specific mount data */
    void *file_private;            /* File-specific data */
    uint32_t offset;               /* Current position */
    int flags;                     /* Open flags */
    struct file_handle *next;      /* Next in list */
} file_handle_t;

/* Mount point entry */
typedef struct {
    char mount_path[MAX_PATH_LEN];
    int fs_id;
    int device_id;
    int partition_id;
    void *fs_private;
    int in_use;
} mount_point_t;

/* Filesystem driver entry */
typedef struct {
    char fs_name[MAX_FS_NAME];
    fs_ops_t ops;
    int in_use;
} fs_driver_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static file_handle_t *file_list_head = NULL;
static int next_fd = 3;  /* Start from 3 (0-2 reserved for stdin/stdout/stderr) */

static mount_point_t mount_table[MAX_MOUNT_POINTS];
static fs_driver_t fs_drivers[MAX_MOUNT_POINTS];  /* Reuse limit for simplicity */

/* =========================================================================
 * File Handle Management (Linked List)
 * ========================================================================= */

static file_handle_t *alloc_file_handle(void)
{
    file_handle_t *fh = (file_handle_t *)kalloc(sizeof(file_handle_t));
    if (!fh) {
        return NULL;
    }
    
    fh->fd = next_fd++;
    fh->fs_id = -1;
    fh->fs_private = NULL;
    fh->file_private = NULL;
    fh->offset = 0;
    fh->flags = 0;
    fh->next = file_list_head;
    file_list_head = fh;
    
    return fh;
}

static file_handle_t *find_file_handle(int fd)
{
    file_handle_t *fh = file_list_head;
    while (fh) {
        if (fh->fd == fd) {
            return fh;
        }
        fh = fh->next;
    }
    return NULL;
}

static void free_file_handle(int fd)
{
    file_handle_t **ptr = &file_list_head;
    while (*ptr) {
        if ((*ptr)->fd == fd) {
            file_handle_t *to_free = *ptr;
            *ptr = to_free->next;
            kfree(to_free);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

/* =========================================================================
 * Mount Point Management
 * ========================================================================= */

/* Find filesystem driver by name */
static int find_fs_driver(const char *fs_name)
{
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (fs_drivers[i].in_use && strcmp(fs_drivers[i].fs_name, fs_name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find mount point by path (longest match) */
static mount_point_t *find_mount_point(const char *path, int *match_len)
{
    mount_point_t *best_match = NULL;
    int best_len = 0;
    
    printk("[VFS] Looking for mount point for path: %s\n", path);
    
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!mount_table[i].in_use) {
            continue;
        }
        
        printk("[VFS]   Checking mount: %s\n", mount_table[i].mount_path);
        
        int mount_len = strlen(mount_table[i].mount_path);
        
        /* Special case for root mount "/" */
        if (mount_len == 1 && mount_table[i].mount_path[0] == '/') {
            /* Root mount matches everything */
            if (mount_len > best_len) {
                best_len = mount_len;
                best_match = &mount_table[i];
                printk("[VFS]     Root mount matches!\n");
            }
            continue;
        }
        
        /* Check if path starts with mount_path */
        if (strncmp(path, mount_table[i].mount_path, mount_len) == 0) {
            /* Exact match or path continues with '/' */
            if (path[mount_len] == '\0' || path[mount_len] == '/') {
                if (mount_len > best_len) {
                    best_len = mount_len;
                    best_match = &mount_table[i];
                    printk("[VFS]     Match found!\n");
                }
            }
        }
    }
    
    if (match_len) {
        *match_len = best_len;
    }
    
    if (best_match) {
        printk("[VFS]   Best match: %s (len=%d)\n", best_match->mount_path, best_len);
    } else {
        printk("[VFS]   No match found!\n");
    }
    
    return best_match;
}

/* =========================================================================
 * Filesystem Registration
 * ========================================================================= */

int register_filesystem(const char *fs_name, fs_ops_t *ops)
{
    if (!fs_name || !ops) {
        return -1;
    }
    
    /* Find free slot */
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!fs_drivers[i].in_use) {
            strcpy(fs_drivers[i].fs_name, fs_name);
            fs_drivers[i].ops = *ops;
            fs_drivers[i].in_use = 1;
            return i;
        }
    }
    
    return -1;  /* No free slots */
}

/* =========================================================================
 * Mount Management
 * ========================================================================= */

int fs_mount(int device_id, int partition_id, const char *fs_type, const char *mount_path)
{
    if (!fs_type || !mount_path) {
        return -1;
    }
    
    /* Find filesystem driver */
    int fs_id = find_fs_driver(fs_type);
    if (fs_id < 0) {
        printk("[VFS] Filesystem type '%s' not registered\n", fs_type);
        return -1;
    }
    
    /* Find free mount point slot */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!mount_table[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        printk("[VFS] No free mount point slots\n");
        return -1;
    }
    
    /* Call filesystem driver's mount function */
    void *fs_private = NULL;
    if (fs_drivers[fs_id].ops.mount) {
        if (fs_drivers[fs_id].ops.mount(device_id, partition_id, &fs_private) != 0) {
            printk("[VFS] Filesystem mount failed\n");
            return -1;
        }
    }
    
    /* Register mount point */
    strcpy(mount_table[slot].mount_path, mount_path);
    mount_table[slot].fs_id = fs_id;
    mount_table[slot].device_id = device_id;
    mount_table[slot].partition_id = partition_id;
    mount_table[slot].fs_private = fs_private;
    mount_table[slot].in_use = 1;
    
    printk("[VFS] Mounted %s (dev %d, part %d) at %s\n",
           fs_type, device_id, partition_id, mount_path);
    
    return 0;
}

int fs_unmount(const char *mount_path)
{
    if (!mount_path) {
        return -1;
    }
    
    /* Find mount point */
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (mount_table[i].in_use && strcmp(mount_table[i].mount_path, mount_path) == 0) {
            /* Call filesystem driver's unmount function */
            int fs_id = mount_table[i].fs_id;
            if (fs_drivers[fs_id].ops.unmount) {
                fs_drivers[fs_id].ops.unmount(mount_table[i].fs_private);
            }
            
            /* Clear mount point */
            mount_table[i].in_use = 0;
            mount_table[i].fs_private = NULL;
            
            printk("[VFS] Unmounted %s\n", mount_path);
            return 0;
        }
    }
    
    return -1;  /* Mount point not found */
}

/* =========================================================================
 * File Operations
 * ========================================================================= */

int fs_open(const char *path, int flags)
{
    if (!path) {
        return -1;
    }
    
    /* Find mount point */
    int match_len = 0;
    mount_point_t *mp = find_mount_point(path, &match_len);
    if (!mp) {
        printk("[VFS] No mount point for path: %s\n", path);
        return -1;
    }
    
    /* Calculate relative path (strip mount point prefix) */
    const char *rel_path = path + match_len;
    if (*rel_path == '\0') {
        rel_path = "/";  /* Root of filesystem */
    }
    
    /* Allocate file handle */
    file_handle_t *fh = alloc_file_handle();
    if (!fh) {
        printk("[VFS] Failed to allocate file handle\n");
        return -1;
    }
    
    /* Call filesystem driver's open function */
    void *file_private = NULL;
    int fs_id = mp->fs_id;
    if (fs_drivers[fs_id].ops.open) {
        if (fs_drivers[fs_id].ops.open(mp->fs_private, rel_path, flags, &file_private) != 0) {
            free_file_handle(fh->fd);
            return -1;
        }
    }
    
    /* Initialize file handle */
    fh->fs_id = fs_id;
    fh->fs_private = mp->fs_private;
    fh->file_private = file_private;
    fh->offset = 0;
    fh->flags = flags;
    
    return fh->fd;
}

int fs_close(int fd)
{
    /* Find file handle */
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) {
        return -1;
    }
    
    /* Call filesystem driver's close function */
    if (fs_drivers[fh->fs_id].ops.close) {
        fs_drivers[fh->fs_id].ops.close(fh->file_private);
    }
    
    /* Free file handle */
    free_file_handle(fd);
    
    return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
    if (!buf) {
        return -1;
    }
    
    /* Find file handle */
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) {
        return -1;
    }
    
    /* Check if opened for reading */
    if ((fh->flags & O_RDWR) == O_WRONLY) {
        return -1;  /* Write-only mode */
    }
    
    /* Call filesystem driver's read function */
    int bytes_read = -1;
    if (fs_drivers[fh->fs_id].ops.read) {
        bytes_read = fs_drivers[fh->fs_id].ops.read(fh->file_private, buf, count);
        if (bytes_read > 0) {
            fh->offset += bytes_read;
        }
    }
    
    return bytes_read;
}

int fs_write(int fd, const void *buf, size_t count)
{
    if (!buf) {
        return -1;
    }
    
    /* Find file handle */
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) {
        return -1;
    }
    
    /* Check if opened for writing */
    if ((fh->flags & O_RDWR) == O_RDONLY) {
        return -1;  /* Read-only mode */
    }
    
    /* Call filesystem driver's write function */
    int bytes_written = -1;
    if (fs_drivers[fh->fs_id].ops.write) {
        bytes_written = fs_drivers[fh->fs_id].ops.write(fh->file_private, buf, count);
        if (bytes_written > 0) {
            fh->offset += bytes_written;
        }
    }
    
    return bytes_written;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void vfs_init(void)
{
    /* Initialize mount table */
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        mount_table[i].in_use = 0;
        mount_table[i].fs_private = NULL;
    }
    
    /* Initialize filesystem driver table */
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        fs_drivers[i].in_use = 0;
    }
    
    /* Initialize file handle list */
    file_list_head = NULL;
    next_fd = 3;
    
    printk("[VFS] Virtual filesystem initialized\n");
}