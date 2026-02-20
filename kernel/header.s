/* Kernel header - must be first in .text section */
.section .text.header,"ax"
.align 16

.global kernel_header
kernel_header:
    .long 0x534B524E          /* Magic: "SKRN" (Scepter KeRNel) */
    .long _kernel_size        /* Kernel size in bytes */
    .long 0xC0100000          /* Entry point (virtual address) */
    .long 0x00000000          /* Alignment padding */
    
    /* Immediately followed by kernel entry point */
    .global _start
    jmp _real_start           /* Jump over header to actual code */