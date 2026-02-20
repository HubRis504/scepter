#include "cpu.h"
#include "vga.h"
#include "tty.h"
#include "printk.h"
#include "pic.h"
#include "pit.h"
#include "buddy.h"
#include "slab.h"
#include "driver.h"
#include "cache.h"
#include "ide.h"
#include "asm.h"

/* =========================================================================
 * Linker-provided symbol: one byte past the end of the kernel image.
 * Declared as a zero-length array so &kernel_end gives the address.
 * The physical address is (uint32_t)&kernel_end - KERNEL_VMA.
 * ========================================================================= */
extern char kernel_end[];
#define KERNEL_VMA  0xC0000000U

/* =========================================================================
 * Global memory information (set once in kernel_main, read-only after that)
 * ========================================================================= */

/* Total detected RAM in kilobytes (from CMOS detection) */
uint32_t mem_total_kb  = 0;

/* Physical address of the first 4 KB-aligned page that is free to use
 * (i.e. the page immediately after the kernel image). */
uint32_t mem_first_free_phys = 0;

/* =========================================================================
 * CMOS Memory Detection
 * ========================================================================= */

static uint32_t detect_memory_cmos(void)
{
    /* Read memory size from CMOS/RTC NVRAM registers */
    
    /* Extended memory (1MB to 16MB) from CMOS registers 0x17-0x18 */
    outb(0x70, 0x17);
    uint32_t low = inb(0x71);
    outb(0x70, 0x18);
    uint32_t high = inb(0x71);
    uint32_t extended_kb = (high << 8) | low;
    
    /* Memory above 16MB from CMOS registers 0x34-0x35 (in 64KB blocks) */
    outb(0x70, 0x34);
    low = inb(0x71);
    outb(0x70, 0x35);
    high = inb(0x71);
    uint32_t above_16mb_kb = ((high << 8) | low) * 64;
    
    /* Total: base 1MB + extended + above_16mb */
    return 1024 + extended_kb + above_16mb_kb;
}

/* =========================================================================
 * kernel_main
 * ========================================================================= */

void kernel_main(void)
{
    gdt_init();
    idt_init();
    isr_init();
    pic_init(0x20, 0x28);
    
    /* Initialize VGA hardware and clear screen FIRST */
    vga_init();

    printk("Scepter i386 Kernel\n\n");

    /* ------------------------------------------------------------------
     * Detect memory size via CMOS
     * ------------------------------------------------------------------ */
    mem_total_kb = detect_memory_cmos();
    printk("[MEM] Detected %u KB (%u MB) via CMOS\n",
           mem_total_kb, mem_total_kb / 1024);

    /* ------------------------------------------------------------------
     * End of kernel image → first free physical page
     *
     * kernel_end is a virtual address in the higher half.
     * Subtract KERNEL_VMA to get the physical address, then round up
     * to the next 4 KB page boundary.
     * ------------------------------------------------------------------ */
    uint32_t kernel_end_phys = (uint32_t)kernel_end - KERNEL_VMA;
    mem_first_free_phys = (kernel_end_phys + 0xFFF) & ~0xFFFU;  /* page-align up */

    printk("[MEM] kernel image end: phys=0x%08x  virt=0x%08x\n",
           kernel_end_phys, (uint32_t)kernel_end);
    printk("[MEM] first free page:  phys=0x%08x\n\n",
           mem_first_free_phys);

    /* ------------------------------------------------------------------
     * All physical memory (0-1GB) is pre-mapped by boot.s
     * Virtual 0xC0000000-0xFFFFFFFF → Physical 0x00000000-0x3FFFFFFF
     *
     * Initialize buddy allocator with available memory.
     * Protection: Only use memory that exists (detected via CMOS)
     * Note: Low memory (0-1MB) is NOT used in this project
     * ------------------------------------------------------------------ */
    uint32_t max_phys_mapped = 0x40000000;  /* 1 GB pre-mapped region end */
    uint32_t max_phys_detected = mem_total_kb * 1024;  /* Actual RAM size */
    
    /* Use the minimum of mapped region and detected memory */
    uint32_t max_phys = (max_phys_detected < max_phys_mapped) 
                        ? max_phys_detected 
                        : max_phys_mapped;
    
    /* Calculate memory available for buddy allocator */
    /* Start from first free page after kernel (guaranteed >= 1MB) */
    uint32_t buddy_mem_kb = (max_phys - mem_first_free_phys) / 1024;
    
    printk("[MEM] Pre-mapped region: phys 0x00000000-0x3FFFFFFF (1 GB)\n");
    printk("[MEM] Detected RAM: %u KB (%u MB)\n", 
           mem_total_kb, mem_total_kb / 1024);
    printk("[MEM] Usable limit: phys 0x%08x (%u MB)\n",
           max_phys, max_phys / (1024 * 1024));
    printk("[MEM] Low memory (0-1MB): RESERVED, not used\n");
    printk("[MEM] Buddy allocator range: phys 0x%08x-0x%08x\n",
           mem_first_free_phys, max_phys);
    printk("[MEM] Buddy allocator memory: %u KB (%u MB)\n",
           buddy_mem_kb, buddy_mem_kb / 1024);
    
    buddy_init(mem_first_free_phys, buddy_mem_kb);
    
    /* Initialize slab allocator */
    slab_init();
    
    /* Initialize block device cache */
    cache_init();
    
    /* Initialize driver subsystem and register devices */
    driver_init();
    vga_register_driver();
    
    /* Initialize TTY after VGA */
    tty_init();
    tty_register_driver();
    
    pit_init(100);
    pit_register_driver();

    printk("Early initialization complete.\n\n");
    
    /* Initialize IDE driver */
    ide_init();
    ide_register_driver();
    ide_print_disks();
    
    /* ================================================================
     * IDE + CACHE INTEGRATION TEST
     * ================================================================ */
    printk("\n=== IDE + CACHE TEST ===\n");
    
    /* Check if hda exists */
    if (ide_disks[0].exists) {
        uint8_t buf1[512], buf2[512], buf3[512];
        uint32_t hits_before, misses_before, entries_before;
        uint32_t hits_after, misses_after, entries_after;
        
        /* Test 1: Read sector 0 from hda (cache miss expected) */
        printk("Test 1: Read sector 0 (cache miss)\n");
        cache_stats(&hits_before, &misses_before, &entries_before);
        
        if (bread(0, 0, buf1, 512) == 512) {
            cache_stats(&hits_after, &misses_after, &entries_after);
            if (misses_after == misses_before + 1) {
                printk("  Result: PASS (cache miss, data read from disk)\n");
            } else {
                printk("  Result: FAIL (expected cache miss)\n");
            }
        } else {
            printk("  Result: FAIL (read failed)\n");
        }
        
        /* Test 2: Read same sector again (cache hit expected) */
        printk("\nTest 2: Read sector 0 again (cache hit)\n");
        cache_stats(&hits_before, &misses_before, &entries_before);
        
        if (bread(0, 0, buf2, 512) == 512) {
            cache_stats(&hits_after, &misses_after, &entries_after);
            
            /* Verify data matches */
            int match = 1;
            for (int i = 0; i < 512; i++) {
                if (buf1[i] != buf2[i]) {
                    match = 0;
                    break;
                }
            }
            
            if (hits_after == hits_before + 1 && match) {
                printk("  Result: PASS (cache hit, data matches)\n");
            } else if (!match) {
                printk("  Result: FAIL (data mismatch)\n");
            } else {
                printk("  Result: FAIL (expected cache hit)\n");
            }
        } else {
            printk("  Result: FAIL (read failed)\n");
        }
        
        /* Test 3: Read different sector (cache miss) */
        printk("\nTest 3: Read sector 100 (cache miss)\n");
        cache_stats(&hits_before, &misses_before, &entries_before);
        
        if (bread(0, 100, buf3, 512) == 512) {
            cache_stats(&hits_after, &misses_after, &entries_after);
            if (misses_after == misses_before + 1) {
                printk("  Result: PASS (cache miss for different sector)\n");
            } else {
                printk("  Result: FAIL (expected cache miss)\n");
            }
        } else {
            printk("  Result: FAIL (read failed)\n");
        }
        
        /* Test 4: Read multiple sectors to test cache */
        printk("\nTest 4: Read 10 sectors multiple times\n");
        int test4_pass = 1;
        
        /* First pass - all misses */
        for (int i = 200; i < 210; i++) {
            if (bread(0, i, buf1, 512) != 512) {
                test4_pass = 0;
                break;
            }
        }
        
        /* Second pass - all hits */
        cache_stats(&hits_before, &misses_before, &entries_before);
        for (int i = 200; i < 210; i++) {
            if (bread(0, i, buf1, 512) != 512) {
                test4_pass = 0;
                break;
            }
        }
        cache_stats(&hits_after, &misses_after, &entries_after);
        
        if (test4_pass && (hits_after - hits_before) == 10) {
            printk("  Result: PASS (10 hits on second pass)\n");
        } else {
            printk("  Result: FAIL\n");
        }
        
        /* Test 5: Overall cache statistics */
        printk("\nTest 5: Cache statistics\n");
        cache_stats(&hits_after, &misses_after, &entries_after);
        printk("  Hits: %u, Misses: %u, Entries: %u\n", 
               hits_after, misses_after, entries_after);
        
        if (hits_after + misses_after > 0) {
            uint32_t hit_rate = (hits_after * 100) / (hits_after + misses_after);
            printk("  Hit rate: %u%%\n", hit_rate);
            
            if (hit_rate >= 50) {
                printk("  Result: PASS (good hit rate)\n");
            } else {
                printk("  Result: WARN (low hit rate)\n");
            }
        }
        
        printk("\n=== IDE + CACHE TEST COMPLETE ===\n\n");
    } else {
        printk("\n[IDE] No hda detected, skipping cache test\n\n");
    }
    
    printk("Kernel initialization complete.\n\n");
    
    /* Enable interrupts after all initialization is complete */

    /* ===================================================================
     * RAW IDE TEST - Direct hardware I/O (no driver functions)
     * =================================================================== */
    printk("\n=== RAW IDE TEST ===\n");
    
    #define IDE_BASE 0x1F0
    #define IDE_REG_DATA     (IDE_BASE + 0)
    #define IDE_REG_STATUS   (IDE_BASE + 7)
    #define IDE_REG_COMMAND  (IDE_BASE + 7)
    #define IDE_REG_DRIVE    (IDE_BASE + 6)
    #define IDE_REG_SECCOUNT (IDE_BASE + 2)
    #define IDE_REG_LBA_LOW  (IDE_BASE + 3)
    #define IDE_REG_LBA_MID  (IDE_BASE + 4)
    #define IDE_REG_LBA_HIGH (IDE_BASE + 5)
    
    /* Wait for not busy */
    printk("Waiting for drive ready...\n");
    int wait_count = 0;
    while (inb(IDE_REG_STATUS) & 0x80) {
        wait_count++;
        if (wait_count > 1000000) {
            printk("Timeout waiting for BSY=0!\n");
            break;
        }
    }
    printk("Drive ready after %d loops\n", wait_count);
    
    /* Select drive 0, LBA mode */
    outb(IDE_REG_DRIVE, 0xE0);  /* Master, LBA */
    printk("Selected drive 0\n");
    
    /* Wait a bit */
    for (int i = 0; i < 1000; i++) inb(IDE_REG_STATUS);
    
    /* Send read command for LBA 1 */
    outb(IDE_REG_SECCOUNT, 1);    /* 1 sector */
    outb(IDE_REG_LBA_LOW, 0);     /* LBA = 1 */
    outb(IDE_REG_LBA_MID, 0);
    outb(IDE_REG_LBA_HIGH, 0);
    outb(IDE_REG_COMMAND, 0x20);  /* READ SECTORS */
    
    printk("Sent READ command for LBA 1, waiting for DRQ...\n");
    
    /* Wait for BSY=0, DRQ=1 */
    int timeout = 1000000;
    uint8_t status;
    while (timeout-- > 0) {
        status = inb(IDE_REG_STATUS);
        if (!(status & 0x80) && (status & 0x08)) break;  /* BSY=0, DRQ=1 */
    }
    
    printk("Status = 0x%02x (remaining timeout=%d)\n", status, timeout);
    
    if (timeout > 0 && (status & 0x08)) {
        printk("SUCCESS! DRQ is set, reading data...\n");
        uint16_t data[16];
        for (int i = 0; i < 16; i++) {
            data[i] = inw(IDE_REG_DATA);
        }
        printk("First 16 words: ");
        for (int i = 0; i < 16; i++) {
            printk("%04x ", data[i]);
        }
        printk("\n");
    } else {
        printk("TIMEOUT or NO DRQ! status=0x%02x\n", status);
        if (status & 0x01) printk("  ERR bit is set!\n");
        if (status & 0x80) printk("  BSY bit is still set!\n");
        if (!(status & 0x08)) printk("  DRQ bit is NOT set!\n");
    }

    while(1);
}