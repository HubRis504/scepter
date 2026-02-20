/* Linker script for Scepter kernel with custom bootloader */

KERNEL_VMA = 0xC0000000;
KERNEL_LMA = 0x00100000;

ENTRY(_start)

SECTIONS
{
    /* Physical address space for early boot code */
    . = KERNEL_LMA;
    
    /* Header and early boot code at physical address */
    .text.boot : AT(KERNEL_LMA) {
        *(.text.header)           /* 16-byte header */
        *(.text.boot)             /* Early boot code (before paging) */
    }
    
    /* Switch to virtual address space */
    . = KERNEL_VMA + KERNEL_LMA;
    
    /* Main kernel code at virtual address */
    .text : AT(KERNEL_LMA + SIZEOF(.text.boot)) {
        *(.text)
        *(.text.*)
    }
    
    /* Data section (includes rodata, data, bss) */
    . = ALIGN(4096);
    .data : AT(KERNEL_LMA + SIZEOF(.text.boot) + SIZEOF(.text)) {
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        *(.got)
        *(.got.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    
    kernel_end = .;
    
    /* Kernel size */
    _kernel_size = SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.data);
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
        *(.comment)
        *(.debug*)
        *(.gnu.*)
    }
}