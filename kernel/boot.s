/* -----------------------------------------------------------------------
 * Multiboot header
 * ----------------------------------------------------------------------- */
.section .multiboot
.align 4
.long 0x1BADB002          /* magic */
.long 0x00                /* flags */
.long -(0x1BADB002 + 0x00) /* checksum */

/* -----------------------------------------------------------------------
 * 16 KB kernel stack in .bss
 * ----------------------------------------------------------------------- */
.section .bss
.align 16
stack_bottom:
    .skip 16384           /* 16 KB */
stack_top:

/* -----------------------------------------------------------------------
 * Boot entry – runs at physical address (paging not yet enabled)
 * ----------------------------------------------------------------------- */
.section .text
.global _start
_start:
    /* ---- Build PSE page directory at physical 0x0 ---- */

    /* Zero all 1024 entries (4096 bytes) */
    movl  $0x0,   %edi
    movl  $1024,  %ecx
    xorl  %eax,   %eax
    rep   stosl

    /*
     * Entry 0   → identity map 0x00000000 (needed while IP is still low)
     * Entry 768 → higher-half  0xC0000000 → phys 0x00000000
     * Flags: Present(0) | Writable(1) | PSE/4MB(7) = 0x83
     */
    movl  $0x00000083, 0x000          /* PDE[0]   = 0x00000000 | PSE */
    movl  $0x00000083, 0xC00          /* PDE[768] = 0x00000000 | PSE  (768*4 = 0xC00) */

    /* ---- Load CR3 with page directory physical address ---- */
    movl  $0x0, %eax
    movl  %eax, %cr3

    /* ---- Enable PSE (4 MB pages) in CR4 bit 4 ---- */
    movl  %cr4, %eax
    orl   $0x10, %eax
    movl  %eax, %cr4

    /* ---- Enable paging in CR0 bit 31 ---- */
    movl  %cr0, %eax
    orl   $0x80000000, %eax
    movl  %eax, %cr0

    /* ---- Far jump to flush pipeline and land in higher half ---- */
    ljmp  $0x08, $_start_higher

/* -----------------------------------------------------------------------
 * Now running at virtual 0xC0xxxxxx
 * ----------------------------------------------------------------------- */
_start_higher:
    /* Remove identity mapping (entry 0) */
    movl  $0x0, 0x000

    /* Flush TLB by reloading CR3 */
    movl  %cr3, %eax
    movl  %eax, %cr3

    /* Set up the kernel stack */
    movl  $stack_top, %esp

    /* Push multiboot info (ebx = multiboot info pointer, still physical) */
    pushl %ebx

    /* Call the C kernel */
    call  kernel_main

    /* Halt if kernel_main ever returns */
    cli
1:  hlt
    jmp   1b
