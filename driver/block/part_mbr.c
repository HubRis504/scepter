#include "driver/block/part_mbr.h"
#include "driver/driver.h"
#include "driver/block/ide.h"
#include "lib/printk.h"
#include <stddef.h>

/* =========================================================================
 * Global Partition Table
 * 
 * Store partition info for all disks (4 disks × 4 partitions = 16 max)
 * ========================================================================= */

static partition_info_t partitions[IDE_MAX_DISKS][MBR_PARTITION_COUNT];

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * Parse MBR from a disk and populate partition table
 * Returns number of valid partitions found
 */
static int mbr_parse_disk(uint8_t disk_id)
{
    uint8_t mbr_buffer[512];
    mbr_t *mbr = (mbr_t *)mbr_buffer;
    int partition_count = 0;
    
    /* TEMPORARY: Use direct IDE read to bypass cache/abstraction for debugging */
    if (ide_read_sectors(disk_id, 0, 1, mbr_buffer) != 0) {
        printk("[MBR] Failed to read MBR from disk %d\n", disk_id);
        return 0;
    }
    

    /* Verify MBR signature */
    if (mbr->signature != MBR_SIGNATURE) {
        printk("[MBR] Invalid MBR signature on disk %d (0x%04x)\n", 
               disk_id, mbr->signature);
        return 0;
    }
    
    /* Parse partition entries */
    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        mbr_partition_entry_t *entry = &mbr->partitions[i];
        partition_info_t *part = &partitions[disk_id][i];
        
        /* Check if partition exists (non-zero type) */
        if (entry->type != PART_TYPE_EMPTY && entry->lba_count > 0) {
            part->valid = true;
            part->disk_id = disk_id;
            part->partition_num = i + 1;  /* 1-based partition numbering */
            part->type = entry->type;
            part->bootable = (entry->status == 0x80);
            part->lba_start = entry->lba_start;
            part->lba_count = entry->lba_count;
            partition_count++;
        } else {
            part->valid = false;
        }
    }
    
    return partition_count;
}

/**
 * Get partition type name for printing
 */
static const char *mbr_get_type_name(uint8_t type)
{
    switch (type) {
        case PART_TYPE_EMPTY:       return "Empty";
        case PART_TYPE_FAT16_LBA:   return "FAT16-LBA";
        case PART_TYPE_FAT32_LBA:   return "FAT32-LBA";
        case PART_TYPE_NTFS:        return "NTFS";
        case PART_TYPE_MINIX:       return "Minix";
        case PART_TYPE_LINUX:       return "Linux";
        case PART_TYPE_LINUX_SWAP:  return "Linux Swap";
        case PART_TYPE_EXTENDED:    return "Extended";
        default:                    return "Unknown";
    }
}

/* =========================================================================
 * Block Device Callbacks for Partitions
 * ========================================================================= */

/**
 * Partition block device read callback
 * prim_id: 4=hdaX, 5=hdbX, 6=hdcX, 7=hddX
 * scnd_id: partition number (1-4), 0 is invalid
 */
static int part_block_read(int prim_id, int scnd_id, void *buf, size_t count)
{
    /* Map prim_id to disk_id (4->0, 5->1, 6->2, 7->3) */
    int disk_id = prim_id - 4;
    
    /* Validate disk_id */
    if (disk_id < 0 || disk_id >= IDE_MAX_DISKS) {
        return -1;
    }
    
    /* Validate partition number (1-4, 0 is invalid) */
    if (scnd_id < 1 || scnd_id > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[disk_id][scnd_id - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* Only handle 512-byte sector reads */
    if (count != 512) {
        return -1;
    }
    
    /* Chain to underlying disk through abstraction layer
     * Add partition offset to get absolute LBA sector */
    uint32_t absolute_lba = part->lba_start;
    
    return bread(disk_id, absolute_lba, buf, count);
}

/**
 * Partition block device write callback
 * prim_id: 4=hdaX, 5=hdbX, 6=hdcX, 7=hddX
 * scnd_id: partition number (1-4), 0 is invalid
 */
static int part_block_write(int prim_id, int scnd_id, const void *buf, size_t count)
{
    /* Map prim_id to disk_id (4->0, 5->1, 6->2, 7->3) */
    int disk_id = prim_id - 4;
    
    /* Validate disk_id */
    if (disk_id < 0 || disk_id >= IDE_MAX_DISKS) {
        return -1;
    }
    
    /* Validate partition number (1-4, 0 is invalid) */
    if (scnd_id < 1 || scnd_id > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[disk_id][scnd_id - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* Only handle 512-byte sector writes */
    if (count != 512) {
        return -1;
    }
    
    /* Chain to underlying disk through abstraction layer
     * Add partition offset to get absolute LBA sector */
    uint32_t absolute_lba = part->lba_start;
    
    return bwrite(disk_id, absolute_lba, buf, count);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void mbr_init(void)
{
    printk("[MBR] Scanning partition tables...\n");
    
    block_ops_t part_ops = {
        .read = part_block_read,
        .write = part_block_write
    };
    
    const char *disk_names[] = {"hda", "hdb", "hdc", "hdd"};
    int total_partitions = 0;
    
    /* Initialize partition table */
    for (int disk = 0; disk < IDE_MAX_DISKS; disk++) {
        for (int part = 0; part < MBR_PARTITION_COUNT; part++) {
            partitions[disk][part].valid = false;
        }
    }
    
    /* Scan each IDE disk for partitions */
    for (int disk_id = 0; disk_id < IDE_MAX_DISKS; disk_id++) {
        /* Only scan disks that exist */
        if (!ide_disks[disk_id].exists) {
            continue;
        }
        
        /* Parse MBR */
        int part_count = mbr_parse_disk(disk_id);
        
        if (part_count > 0) {
            /* Register partition block device for this disk */
            int prim_id = 4 + disk_id;  /* hda=4, hdb=5, hdc=6, hdd=7 */
            
            if (register_block_device(prim_id, &part_ops) == 0) {
                printk("[MBR] Registered %sX as block device %d (%d partition%s)\n",
                       disk_names[disk_id], prim_id, part_count,
                       part_count > 1 ? "s" : "");
                total_partitions += part_count;
            } else {
                printk("[MBR] Failed to register %sX\n", disk_names[disk_id]);
            }
        } else {
            printk("[MBR] No valid partitions on %s\n", disk_names[disk_id]);
        }
    }
    
    printk("[MBR] Found %d partition%s total\n", 
           total_partitions, total_partitions != 1 ? "s" : "");
}

void mbr_print_partitions(void)
{
    const char *disk_names[] = {"hda", "hdb", "hdc", "hdd"};
    
    printk("\n=== Partition Table ===\n");
    
    for (int disk_id = 0; disk_id < IDE_MAX_DISKS; disk_id++) {
        if (!ide_disks[disk_id].exists) {
            continue;
        }
        
        bool has_partitions = false;
        for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
            if (partitions[disk_id][i].valid) {
                has_partitions = true;
                break;
            }
        }
        
        if (!has_partitions) {
            continue;
        }
        
        printk("\n%s:\n", disk_names[disk_id]);
        
        for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
            partition_info_t *part = &partitions[disk_id][i];
            
            if (part->valid) {
                uint32_t size_mb = (part->lba_count / 2048);  /* sectors * 512 / (1024*1024) */
                
                printk("  %s%d: %s%s, Start: %u, Size: %u MB (%u sectors)\n",
                       disk_names[disk_id],
                       part->partition_num,
                       part->bootable ? "[BOOT] " : "",
                       mbr_get_type_name(part->type),
                       part->lba_start,
                       size_mb,
                       part->lba_count);
            }
        }
    }
    
    printk("\n");
}

const partition_info_t *mbr_get_partition_info(int device_id, int partition_id)
{
    /* Map device_id to disk_id (4->0, 5->1, 6->2, 7->3) */
    int disk_id = device_id - 4;
    
    /* Validate parameters */
    if (disk_id < 0 || disk_id >= IDE_MAX_DISKS) {
        return NULL;
    }
    
    if (partition_id < 1 || partition_id > MBR_PARTITION_COUNT) {
        return NULL;
    }
    
    partition_info_t *part = &partitions[disk_id][partition_id - 1];
    
    if (!part->valid) {
        return NULL;
    }
    
    return part;
}
