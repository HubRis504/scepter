#ifndef PART_MBR_H
#define PART_MBR_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * MBR (Master Boot Record) Partition Table Support
 *
 * Provides block device access to disk partitions.
 * Device mapping: hdaX=4, hdbX=5, hdcX=6, hddX=7
 * scnd_id is partition number (1-4), scnd_id=0 is invalid
 * ========================================================================= */

/* MBR Constants */
#define MBR_SIGNATURE           0xAA55
#define MBR_PARTITION_COUNT     4
#define MBR_BOOTSTRAP_SIZE      446

/* Partition Types (common ones) */
#define PART_TYPE_EMPTY         0x00
#define PART_TYPE_FAT16_LBA     0x0E
#define PART_TYPE_NTFS          0x07
#define PART_TYPE_FAT32_LBA     0x0C
#define PART_TYPE_MINIX         0x81
#define PART_TYPE_LINUX         0x83
#define PART_TYPE_LINUX_SWAP    0x82
#define PART_TYPE_EXTENDED      0x05

/* =========================================================================
 * MBR Structures
 * ========================================================================= */

/**
 * MBR Partition Entry (16 bytes)
 */
typedef struct {
    uint8_t  status;            /* 0x80 = bootable, 0x00 = not bootable */
    uint8_t  first_chs[3];      /* CHS of first sector (legacy, unused) */
    uint8_t  type;              /* Partition type */
    uint8_t  last_chs[3];       /* CHS of last sector (legacy, unused) */
    uint32_t lba_start;         /* LBA start sector */
    uint32_t lba_count;         /* Number of sectors in partition */
} __attribute__((packed)) mbr_partition_entry_t;

/**
 * Master Boot Record (512 bytes)
 */
typedef struct {
    uint8_t bootstrap[MBR_BOOTSTRAP_SIZE];     /* Boot code */
    mbr_partition_entry_t partitions[MBR_PARTITION_COUNT];  /* 4 partition entries */
    uint16_t signature;                         /* 0xAA55 magic number */
} __attribute__((packed)) mbr_t;

/**
 * Internal partition information
 */
typedef struct {
    bool     valid;             /* True if partition exists */
    uint8_t  disk_id;           /* Underlying disk ID (0-3) */
    uint8_t  partition_num;     /* Partition number (1-4) */
    uint8_t  type;              /* Partition type */
    bool     bootable;          /* True if bootable */
    uint32_t lba_start;         /* Start LBA sector */
    uint32_t lba_count;         /* Number of sectors */
} partition_info_t;

/* =========================================================================
 * Function Prototypes
 * ========================================================================= */

/**
 * Initialize MBR partition support
 * Scans all IDE disks for MBR partition tables and registers partition
 * block devices (prim_id 4-7 for hda-hdd partitions)
 */
void mbr_init(void);

/**
 * Print all detected partitions to console
 */
void mbr_print_partitions(void);

/**
 * Get partition information
 * @param device_id Partition device ID (4-7)
 * @param partition_id Partition number (1-4)
 * @return Partition info or NULL if invalid
 */
const partition_info_t *mbr_get_partition_info(int device_id, int partition_id);

#endif /* PART_MBR_H */
