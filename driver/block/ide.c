#include "ide.h"
#include "asm.h"
#include "printk.h"
#include <stddef.h>

/* =========================================================================
 * Global IDE Disk Array
 * ========================================================================= */

ide_disk_t ide_disks[IDE_MAX_DISKS] = {0};

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * Wait for IDE drive to become ready (BSY=0)
 * Returns 0 on success, -1 on timeout
 */
static int ide_wait_bsy(uint16_t base_port)
{
    int timeout = 100000;
    while ((inb(base_port + IDE_REG_STATUS) & IDE_STATUS_BSY) && timeout > 0) {
        timeout--;
    }
    return (timeout > 0) ? 0 : -1;
}

/**
 * Wait for IDE drive to be ready and have data (BSY=0, DRQ=1)
 * Returns 0 on success, -1 on timeout
 */
static int ide_wait_drq(uint16_t base_port)
{
    if (ide_wait_bsy(base_port) != 0) {
        return -1;
    }
    
    int timeout = 100000;
    while (!(inb(base_port + IDE_REG_STATUS) & IDE_STATUS_DRQ) && timeout > 0) {
        timeout--;
    }
    return (timeout > 0) ? 0 : -1;
}

/**
 * Select IDE drive (master or slave)
 */
static void ide_select_drive(uint16_t base_port, uint16_t ctrl_port, uint8_t drive)
{
    if (drive == 0) {
        outb(base_port + IDE_REG_DRIVE, IDE_DRIVE_MASTER);
    } else {
        outb(base_port + IDE_REG_DRIVE, IDE_DRIVE_SLAVE);
    }
    
    /* Delay 400ns for drive selection to take effect */
    /* Read from alternate status register (control port) */
    for (int i = 0; i < 4; i++) {
        inb(ctrl_port + IDE_REG_ALTSTATUS);
    }
}

/**
 * Identify IDE disk and fill disk structure
 * Returns true if disk exists, false otherwise
 */
static bool ide_identify(uint16_t base_port, uint16_t ctrl_port, uint8_t drive, ide_disk_t *disk)
{
    uint16_t identify_data[256];
    
    /* Select drive */
    ide_select_drive(base_port, ctrl_port, drive);
    
    /* Send IDENTIFY command */
    outb(base_port + IDE_REG_SECCOUNT, 0);
    outb(base_port + IDE_REG_LBA_LOW, 0);
    outb(base_port + IDE_REG_LBA_MID, 0);
    outb(base_port + IDE_REG_LBA_HIGH, 0);
    outb(base_port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    
    /* Check if drive exists (floating bus returns 0xFF or 0x00) */
    uint8_t status = inb(base_port + IDE_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return false;  /* No drive - floating bus */
    }
    
    /* Wait for BSY to clear with timeout */
    if (ide_wait_bsy(base_port) != 0) {
        return false;  /* Timeout waiting for drive */
    }
    
    /* Check for error */
    status = inb(base_port + IDE_REG_STATUS);
    if (status & IDE_STATUS_ERR) {
        return false;  /* Error or ATAPI device */
    }
    
    /* Wait for DRQ (data ready) with timeout */
    if (ide_wait_drq(base_port) != 0) {
        return false;  /* Timeout waiting for data */
    }
    
    /* Read 256 words (512 bytes) of identification data */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base_port + IDE_REG_DATA);
    }
    
    /* Fill disk structure */
    disk->exists = true;
    disk->base_port = base_port;
    disk->ctrl_port = ctrl_port;
    disk->drive = drive;
    
    /* Get total sectors (LBA28) from words 60-61 */
    disk->sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];
    
    /* Extract model string from words 27-46 (40 bytes) */
    for (int i = 0; i < 20; i++) {
        disk->model[i * 2] = (char)(identify_data[27 + i] >> 8);
        disk->model[i * 2 + 1] = (char)(identify_data[27 + i] & 0xFF);
    }
    disk->model[40] = '\0';
    
    /* Trim trailing spaces from model string */
    for (int i = 39; i >= 0; i--) {
        if (disk->model[i] == ' ') {
            disk->model[i] = '\0';
        } else {
            break;
        }
    }
    
    return true;
}

/* =========================================================================
 * Public API Functions
 * ========================================================================= */

void ide_init(void)
{
    printk("[IDE] Scanning for disks...\n");
    
    int disk_count = 0;
    
    /* Scan Primary Master (disk 0) */
    if (ide_identify(IDE_PRIMARY_BASE, IDE_PRIMARY_CTRL, 0, &ide_disks[0])) {
        disk_count++;
    }
    
    /* Scan Primary Slave (disk 1) */
    if (ide_identify(IDE_PRIMARY_BASE, IDE_PRIMARY_CTRL, 1, &ide_disks[1])) {
        disk_count++;
    }
    
    /* Scan Secondary Master (disk 2) */
    if (ide_identify(IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL, 0, &ide_disks[2])) {
        disk_count++;
    }
    
    /* Scan Secondary Slave (disk 3) */
    if (ide_identify(IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL, 1, &ide_disks[3])) {
        disk_count++;
    }
    
    printk("[IDE] Found %d disk(s)\n", disk_count);
}

void ide_print_disks(void)
{
    const char *position[] = {
        "Primary Master",
        "Primary Slave",
        "Secondary Master",
        "Secondary Slave"
    };
    
    for (int i = 0; i < IDE_MAX_DISKS; i++) {
        if (ide_disks[i].exists) {
            uint32_t size_mb = (ide_disks[i].sectors / 2048);  /* sectors * 512 / (1024*1024) */
            printk("[IDE] Disk %d (%s): %s, %u MB (%u sectors)\n",
                   i, position[i], ide_disks[i].model, size_mb, ide_disks[i].sectors);
        }
    }
}

int ide_read_sectors(uint8_t disk_id, uint32_t lba, uint8_t count, void *buffer)
{
    if (disk_id >= IDE_MAX_DISKS || !ide_disks[disk_id].exists) {
        return -1;  /* Invalid disk */
    }
    
    if (count == 0) {
        count = 1;  /* Read at least 1 sector */
    }
    
    ide_disk_t *disk = &ide_disks[disk_id];
    uint16_t *buf = (uint16_t *)buffer;
    
    /* Wait for drive to be ready */
    ide_wait_bsy(disk->base_port);
    
    /* Select drive and set LBA mode */
    uint8_t drive_bits = (disk->drive == 0) ? IDE_DRIVE_MASTER : IDE_DRIVE_SLAVE;
    drive_bits |= ((lba >> 24) & 0x0F);  /* LBA bits 24-27 */
    outb(disk->base_port + IDE_REG_DRIVE, drive_bits);
    
    /* Send sector count and LBA */
    outb(disk->base_port + IDE_REG_SECCOUNT, count);
    outb(disk->base_port + IDE_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send READ command */
    outb(disk->base_port + IDE_REG_COMMAND, IDE_CMD_READ_PIO);
    
    /* Read sectors */
    for (uint8_t sector = 0; sector < count; sector++) {
        /* Wait for data to be ready */
        ide_wait_drq(disk->base_port);
        
        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(disk->base_port + IDE_REG_DATA);
        }
    }
    
    return 0;
}

int ide_write_sectors(uint8_t disk_id, uint32_t lba, uint8_t count, const void *buffer)
{
    if (disk_id >= IDE_MAX_DISKS || !ide_disks[disk_id].exists) {
        return -1;  /* Invalid disk */
    }
    
    if (count == 0) {
        count = 1;  /* Write at least 1 sector */
    }
    
    ide_disk_t *disk = &ide_disks[disk_id];
    const uint16_t *buf = (const uint16_t *)buffer;
    
    /* Wait for drive to be ready */
    ide_wait_bsy(disk->base_port);
    
    /* Select drive and set LBA mode */
    uint8_t drive_bits = (disk->drive == 0) ? IDE_DRIVE_MASTER : IDE_DRIVE_SLAVE;
    drive_bits |= ((lba >> 24) & 0x0F);  /* LBA bits 24-27 */
    outb(disk->base_port + IDE_REG_DRIVE, drive_bits);
    
    /* Send sector count and LBA */
    outb(disk->base_port + IDE_REG_SECCOUNT, count);
    outb(disk->base_port + IDE_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send WRITE command */
    outb(disk->base_port + IDE_REG_COMMAND, IDE_CMD_WRITE_PIO);
    
    /* Write sectors */
    for (uint8_t sector = 0; sector < count; sector++) {
        /* Wait for drive to be ready */
        ide_wait_drq(disk->base_port);
        
        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(disk->base_port + IDE_REG_DATA, buf[sector * 256 + i]);
        }
        
        /* Flush write cache */
        outb(disk->base_port + IDE_REG_COMMAND, 0xE7);  /* CACHE FLUSH */
        ide_wait_bsy(disk->base_port);
    }
    
    return 0;
}