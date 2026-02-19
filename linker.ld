KERNEL_VMA = 0xC0000000;

ENTRY(_start)

SECTIONS
{
    . = 0xC0100000;

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.multiboot)
        *(.text)
    }

    .data : AT(ADDR(.data) - KERNEL_VMA) { *(.data) }

    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        *(.bss)
        *(COMMON)
    }

    /* One-past-end marker used by kernel.c to find the first free page */
    kernel_end = .;
}
