KERNEL_VMA = 0xC0000000;

ENTRY(_start)

SECTIONS
{
    /*
     * .boot and .multiboot are linked AND loaded at physical 0x00100000.
     * VMA == LMA here, so the ELF entry point (e_entry = _start) is a
     * valid physical address that GRUB can jump to directly.
     */
    . = 0x00100000;

    .boot : {
        *(.multiboot)
        *(.boot)
    }

    /*
     * The rest of the kernel is linked at the higher-half virtual address
     * but loaded (LMA) at the corresponding physical address just after .boot.
     */
    . = 0xC0100000 + SIZEOF(.boot);

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text)
    }

    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data)
    }

    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        *(.bss)
        *(COMMON)
    }
}
