#include "fat32.h"
#include "fs.h"
#include "driver.h"
#include "slab.h"
#include "printk.h"
#include "part_mbr.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * String utilities
 * ========================================================================= */

static size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
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

static char toupper(char c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* =========================================================================
 * FAT32 Mount State
 * ========================================================================= */

typedef struct {
    int device_id;
    int partition_id;
    fat32_boot_sector_t boot;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_dir_cluster;
    uint32_t bytes_per_cluster;
} fat32_mount_t;

/* =========================================================================
 * FAT32 File State
 * ========================================================================= */

typedef struct {
    fat32_mount_t *mount;
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t position;
    uint32_t cluster_offset;  /* Offset within current cluster */
} fat32_file_t;

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/* Read a sector from the partition 
 * Uses bread() abstraction - partition layer handles LBA translation
 */
static int read_sector(int dev_id, int part_id, uint32_t sector, void *buf)
{
    /* Get partition information to access underlying disk */
    const partition_info_t *part = mbr_get_partition_info(dev_id, part_id);
    if (!part) {
        return -1;
    }
    
    /* Calculate absolute LBA: partition start + sector offset */
    uint32_t absolute_lba = part->lba_start + sector;
    
    /* Use bread() on the disk device (proper abstraction) */
    return bread(part->disk_id, absolute_lba, buf, 512);
}

/* Write a sector to the partition 
 * Uses bwrite() abstraction - partition layer handles LBA translation
 */
static int write_sector(int dev_id, int part_id, uint32_t sector, const void *buf)
{
    /* Get partition information to access underlying disk */
    const partition_info_t *part = mbr_get_partition_info(dev_id, part_id);
    if (!part) {
        return -1;
    }
    
    /* Calculate absolute LBA: partition start + sector offset */
    uint32_t absolute_lba = part->lba_start + sector;
    
    /* Use bwrite() on the disk device (proper abstraction) */
    return bwrite(part->disk_id, absolute_lba, buf, 512);
}

/* Get cluster number from directory entry */
static uint32_t get_cluster(fat32_dir_entry_t *entry)
{
    return ((uint32_t)entry->first_cluster_hi << 16) | entry->first_cluster_lo;
}

/* Read FAT entry to get next cluster in chain */
static uint32_t read_fat_entry(fat32_mount_t *mount, uint32_t cluster)
{
    /* Each FAT entry is 4 bytes */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = mount->fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    uint8_t buf[512];
    if (read_sector(mount->device_id, mount->partition_id, fat_sector, buf) < 0) {
        return 0;
    }
    
    uint32_t value = *(uint32_t *)(buf + entry_offset);
    return value & 0x0FFFFFFF;  /* Mask off top 4 bits */
}

/* Convert 8.3 filename to FAT format (uppercase, space-padded) */
static void name_to_83(const char *name, char *out_83)
{
    int i, j;
    
    /* Initialize with spaces */
    for (i = 0; i < 11; i++) {
        out_83[i] = ' ';
    }
    
    /* Copy name part (up to 8 chars) */
    for (i = 0; i < 8 && name[i] && name[i] != '.'; i++) {
        out_83[i] = toupper(name[i]);
    }
    
    /* Find extension */
    const char *ext = name;
    while (*ext && *ext != '.') ext++;
    if (*ext == '.') ext++;
    
    /* Copy extension (up to 3 chars) */
    for (j = 0; j < 3 && ext[j]; j++) {
        out_83[8 + j] = toupper(ext[j]);
    }
}

/* Compare two 8.3 filenames */
static int compare_83(const char *name1, const char *name2)
{
    return strncmp(name1, name2, 11) == 0;
}

/* Read a cluster */
static int read_cluster(fat32_mount_t *mount, uint32_t cluster, void *buf)
{
    if (cluster < 2) {
        return -1;  /* Invalid cluster */
    }
    
    uint32_t first_sector = mount->data_start_sector + 
                           ((cluster - 2) * mount->boot.sectors_per_cluster);
    
    /* Read all sectors in cluster */
    for (int i = 0; i < mount->boot.sectors_per_cluster; i++) {
        if (read_sector(mount->device_id, mount->partition_id, 
                       first_sector + i, 
                       (uint8_t *)buf + (i * 512)) < 0) {
            return -1;
        }
    }
    
    return mount->bytes_per_cluster;
}

/* Write a cluster */
static int write_cluster(fat32_mount_t *mount, uint32_t cluster, const void *buf)
{
    if (cluster < 2) {
        return -1;  /* Invalid cluster */
    }
    
    uint32_t first_sector = mount->data_start_sector + 
                           ((cluster - 2) * mount->boot.sectors_per_cluster);
    
    /* Write all sectors in cluster */
    for (int i = 0; i < mount->boot.sectors_per_cluster; i++) {
        if (write_sector(mount->device_id, mount->partition_id, 
                        first_sector + i, 
                        (const uint8_t *)buf + (i * 512)) < 0) {
            return -1;
        }
    }
    
    return mount->bytes_per_cluster;
}

/* Find file/directory in a directory cluster */
static fat32_dir_entry_t *find_in_directory(fat32_mount_t *mount, uint32_t dir_cluster, 
                                             const char *name_83)
{
    static uint8_t cluster_buf[32768];  /* Max cluster size */
    static fat32_dir_entry_t found_entry;
    
    uint32_t cluster = dir_cluster;
    
    while (cluster < FAT32_EOC) {
        /* Read directory cluster */
        if (read_cluster(mount, cluster, cluster_buf) < 0) {
            return NULL;
        }
        
        /* Search entries */
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)cluster_buf;
        int num_entries = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        
        for (int i = 0; i < num_entries; i++) {
            /* End of directory */
            if (entries[i].name[0] == 0x00) {
                return NULL;
            }
            
            /* Deleted entry */
            if (entries[i].name[0] == 0xE5) {
                continue;
            }
            
            /* Skip LFN entries and volume labels */
            if (entries[i].attr == ATTR_LONG_NAME || 
                (entries[i].attr & ATTR_VOLUME_ID)) {
                continue;
            }
            
            /* Compare names */
            if (compare_83((char *)entries[i].name, name_83)) {
                found_entry = entries[i];
                return &found_entry;
            }
        }
        
        /* Follow cluster chain */
        cluster = read_fat_entry(mount, cluster);
    }
    
    return NULL;
}

/* =========================================================================
 * FAT32 Operations
 * ========================================================================= */

static int fat32_mount(int dev_id, int part_id, void **fs_private)
{
    fat32_boot_sector_t boot;
    
    /* Read boot sector */
    if (read_sector(dev_id, part_id, 0, &boot) < 0) {
        printk("[FAT32] Failed to read boot sector\n");
        return -1;
    }
    
    /* Validate FAT32 signature */
    if (boot.signature != FAT32_SIGNATURE) {
        printk("[FAT32] Invalid signature: 0x%04x\n", boot.signature);
        return -1;
    }
    
    /* Validate it's FAT32 */
    if (boot.root_entry_count != 0 || boot.fat_size_16 != 0) {
        printk("[FAT32] Not FAT32 (looks like FAT12/16)\n");
        return -1;
    }
    
    /* Allocate mount structure */
    fat32_mount_t *mount = (fat32_mount_t *)kalloc(sizeof(fat32_mount_t));
    if (!mount) {
        printk("[FAT32] Failed to allocate mount structure\n");
        return -1;
    }
    
    /* Initialize mount */
    mount->device_id = dev_id;
    mount->partition_id = part_id;
    mount->boot = boot;
    mount->fat_start_sector = boot.reserved_sectors;
    mount->data_start_sector = boot.reserved_sectors + 
                               (boot.num_fats * boot.fat_size_32);
    mount->root_dir_cluster = boot.root_cluster;
    mount->bytes_per_cluster = boot.bytes_per_sector * boot.sectors_per_cluster;
    
    *fs_private = mount;
    
    printk("[FAT32] Mounted successfully\n");
    printk("[FAT32]   Bytes/sector: %u\n", boot.bytes_per_sector);
    printk("[FAT32]   Sectors/cluster: %u\n", boot.sectors_per_cluster);
    printk("[FAT32]   Root cluster: %u\n", boot.root_cluster);
    
    return 0;
}

static int fat32_unmount(void *fs_private)
{
    if (fs_private) {
        kfree(fs_private);
    }
    return 0;
}

static int fat32_open(void *fs_private, const char *path, int flags, void **file_private)
{
    fat32_mount_t *mount = (fat32_mount_t *)fs_private;
    (void)flags;  /* Not used yet */
    
    if (!mount || !path) {
        return -1;
    }
    
    /* Start at root directory */
    uint32_t current_cluster = mount->root_dir_cluster;
    
    /* Parse path components */
    char name_83[12];
    const char *p = path;
    
    /* Skip leading slash */
    if (*p == '/') p++;
    
    while (*p) {
        /* Extract next component */
        const char *start = p;
        while (*p && *p != '/') p++;
        
        /* Convert to 8.3 format */
        char component[256];
        int len = p - start;
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) {
            component[i] = start[i];
        }
        component[len] = '\0';
        
        name_to_83(component, name_83);
        
        /* Search current directory */
        fat32_dir_entry_t *entry = find_in_directory(mount, current_cluster, name_83);
        if (!entry) {
            printk("[FAT32] Not found: %s\n", component);
            return -1;
        }
        
        /* If there's more path, this must be a directory */
        if (*p == '/') {
            if (!(entry->attr & ATTR_DIRECTORY)) {
                printk("[FAT32] Not a directory: %s\n", component);
                return -1;
            }
            current_cluster = get_cluster(entry);
            p++;  /* Skip slash */
        } else {
            /* Found the file! */
            if (entry->attr & ATTR_DIRECTORY) {
                printk("[FAT32] Is a directory: %s\n", component);
                return -1;
            }
            
            /* Allocate file structure */
            fat32_file_t *file = (fat32_file_t *)kalloc(sizeof(fat32_file_t));
            if (!file) {
                return -1;
            }
            
            file->mount = mount;
            file->first_cluster = get_cluster(entry);
            file->current_cluster = file->first_cluster;
            file->file_size = entry->file_size;
            file->position = 0;
            file->cluster_offset = 0;
            
            *file_private = file;
            
            printk("[FAT32] Opened: %s (cluster %u, size %u)\n", 
                   component, file->first_cluster, file->file_size);
            
            return 0;
        }
    }
    
    return -1;
}

static int fat32_close(void *file_private)
{
    if (file_private) {
        kfree(file_private);
    }
    return 0;
}

static int fat32_read(void *file_private, void *buf, size_t count)
{
    fat32_file_t *file = (fat32_file_t *)file_private;
    
    if (!file || !buf) {
        return -1;
    }
    
    /* Check EOF */
    if (file->position >= file->file_size) {
        return 0;
    }
    
    /* Limit read to file size */
    if (file->position + count > file->file_size) {
        count = file->file_size - file->position;
    }
    
    static uint8_t cluster_buf[32768];
    size_t bytes_read = 0;
    
    while (bytes_read < count) {
        /* Read current cluster */
        if (read_cluster(file->mount, file->current_cluster, cluster_buf) < 0) {
            return -1;
        }
        
        /* Calculate how much to read from this cluster */
        uint32_t bytes_in_cluster = file->mount->bytes_per_cluster - file->cluster_offset;
        uint32_t to_read = count - bytes_read;
        if (to_read > bytes_in_cluster) {
            to_read = bytes_in_cluster;
        }
        
        /* Copy data */
        for (uint32_t i = 0; i < to_read; i++) {
            ((uint8_t *)buf)[bytes_read + i] = cluster_buf[file->cluster_offset + i];
        }
        
        bytes_read += to_read;
        file->position += to_read;
        file->cluster_offset += to_read;
        
        /* Move to next cluster if needed */
        if (file->cluster_offset >= file->mount->bytes_per_cluster) {
            file->current_cluster = read_fat_entry(file->mount, file->current_cluster);
            file->cluster_offset = 0;
            
            if (file->current_cluster >= FAT32_EOC) {
                break;  /* EOF */
            }
        }
    }
    
    return bytes_read;
}

static int fat32_write(void *file_private, const void *buf, size_t count)
{
    fat32_file_t *file = (fat32_file_t *)file_private;
    
    if (!file || !buf) {
        return -1;
    }
    
    /* Phase 1: In-place write only - don't extend file */
    if (file->position >= file->file_size) {
        return 0;  /* At or past EOF */
    }
    
    /* Limit write to current file size (no extension) */
    if (file->position + count > file->file_size) {
        count = file->file_size - file->position;
        if (count == 0) return 0;
    }
    
    static uint8_t cluster_buf[32768];
    size_t bytes_written = 0;
    
    while (bytes_written < count) {
        /* Read current cluster (for partial writes) */
        if (read_cluster(file->mount, file->current_cluster, cluster_buf) < 0) {
            return -1;
        }
        
        /* Calculate how much to write to this cluster */
        uint32_t bytes_in_cluster = file->mount->bytes_per_cluster - file->cluster_offset;
        uint32_t to_write = count - bytes_written;
        if (to_write > bytes_in_cluster) {
            to_write = bytes_in_cluster;
        }
        
        /* Modify cluster data */
        for (uint32_t i = 0; i < to_write; i++) {
            cluster_buf[file->cluster_offset + i] = ((const uint8_t *)buf)[bytes_written + i];
        }
        
        /* Write modified cluster back */
        if (write_cluster(file->mount, file->current_cluster, cluster_buf) < 0) {
            return bytes_written > 0 ? bytes_written : -1;
        }
        
        bytes_written += to_write;
        file->position += to_write;
        file->cluster_offset += to_write;
        
        /* Move to next cluster if needed */
        if (file->cluster_offset >= file->mount->bytes_per_cluster) {
            file->current_cluster = read_fat_entry(file->mount, file->current_cluster);
            file->cluster_offset = 0;
            
            if (file->current_cluster >= FAT32_EOC) {
                break;  /* EOF */
            }
        }
    }
    
    return bytes_written;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void fat32_get_ops(fs_ops_t *ops)
{
    ops->mount = fat32_mount;
    ops->unmount = fat32_unmount;
    ops->open = fat32_open;
    ops->close = fat32_close;
    ops->read = fat32_read;
    ops->write = fat32_write;
}