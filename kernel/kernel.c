#include "cpu.h"
#include "vga.h"
#include "printk.h"
#include "pic.h"
#include "pit.h"
#include "multiboot.h"
#include "buddy.h"

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

/* Total detected RAM above 1 MB, in kilobytes (from Multiboot mem_upper) */
uint32_t mem_upper_kb  = 0;

/* Physical address of the first 4 KB-aligned page that is free to use
 * (i.e. the page immediately after the kernel image). */
uint32_t mem_first_free_phys = 0;

/* =========================================================================
 * kernel_main
 * ========================================================================= */

void kernel_main(uint32_t mb_magic, multiboot_info_t *mbi)
{
    gdt_init();
    idt_init();
    isr_init();
    pic_init(0x20, 0x28);
    vga_init();
    pit_init(100);
    sti();

    printk("Hello, Kernel World!\n\n");

    /* ------------------------------------------------------------------
     * Memory information from Multiboot
     *
     * The bootloader passes mbi as a PHYSICAL address. Since we've
     * mapped all physical memory 0-1GB to virtual 0xC0000000-0xFFFFFFFF,
     * we add KERNEL_VMA to convert physical to virtual address.
     * ------------------------------------------------------------------ */
    multiboot_info_t *mbi_virt = (multiboot_info_t *)((uint32_t)mbi + KERNEL_VMA);

    if (mb_magic == MB_MAGIC && (mbi_virt->flags & MB_FLAG_MEM)) {
        mem_upper_kb = mbi_virt->mem_upper;

        printk("[MEM] upper=%u KB (%u MB)  total~=%u MB\n",
               mem_upper_kb,
               mem_upper_kb / 1024,
               (mem_upper_kb + 1024) / 1024);
    } else {
        printk("[MEM] Multiboot memory info unavailable "
               "(magic=0x%08x flags=0x%x)\n",
               mb_magic,
               (mb_magic == MB_MAGIC) ? mbi_virt->flags : 0);
    }

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
     * Initialize buddy allocator with the pre-mapped 1GB region.
     * Give it all memory from end of kernel to end of pre-mapped region.
     * ------------------------------------------------------------------ */
    uint32_t max_phys = 0x40000000;  /* 1 GB pre-mapped region end */
    
    /* Calculate memory available for buddy allocator */
    uint32_t buddy_mem_kb = (max_phys - mem_first_free_phys) / 1024;
    
    printk("[MEM] Pre-mapped region: phys 0x00000000-0x3FFFFFFF (1 GB)\n");
    printk("[MEM] Buddy allocator range: phys 0x%08x-0x%08x\n",
           mem_first_free_phys, max_phys);
    printk("[MEM] Buddy allocator memory: %u KB (%u MB)\n\n",
           buddy_mem_kb, buddy_mem_kb / 1024);
    
    buddy_init(mem_first_free_phys, buddy_mem_kb);

    printk("Kernel initialization complete.\n\n");

    while(1);
}
