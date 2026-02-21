#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "fs.h"

/* =========================================================================
 * FAT32 Filesystem Driver
 *
 * Read-only FAT32 implementation for VFS.
 * Supports:
 * - 8.3 filenames (no LFN yet)
 * - Directory traversal
 * - File reading
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * FAT32 Boot Sector Structure
 * ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;      /* 0 for FAT32 */
    uint16_t total_sectors_16;      /* 0 for FAT32 */
    uint8_t  media_type;
    uint16_t fat_size_16;           /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    /* FAT32-specific fields */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
    uint8_t  boot_code[420];
    uint16_t signature;
} fat32_boot_sector_t;

/* -------------------------------------------------------------------------
 * FAT32 Directory Entry (32 bytes)
 * ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t  name[11];              /* 8.3 filename */
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} fat32_dir_entry_t;

/* -------------------------------------------------------------------------
 * FAT32 Attributes
 * ------------------------------------------------------------------------- */

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  0x0F

/* -------------------------------------------------------------------------
 * FAT32 Constants
 * ------------------------------------------------------------------------- */

#define FAT32_EOC       0x0FFFFFF8  /* End of chain marker */
#define FAT32_BAD       0x0FFFFFF7  /* Bad cluster marker */
#define FAT32_FREE      0x00000000  /* Free cluster */

#define FAT32_SIGNATURE 0xAA55

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Get FAT32 filesystem operations structure
 * Used to register with VFS
 */
void fat32_get_ops(struct fs_ops *ops);

#endif /* FAT32_H */