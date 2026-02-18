#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include "asm.h"

/* =========================================================================
 * GDT
 * ========================================================================= */

typedef struct {
    uint16_t limit_low;    /* bits 15:0  of segment limit */
    uint16_t base_low;     /* bits 15:0  of base address  */
    uint8_t  base_mid;     /* bits 23:16 of base address  */
    uint8_t  access;       /* P | DPL(2) | S | Type(4)    */
    uint8_t  granularity;  /* G | DB | L | AVL | limit_high(4) */
    uint8_t  base_high;    /* bits 31:24 of base address  */
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;        /* size of GDT - 1 */
    uint32_t base;         /* linear address of GDT */
} __attribute__((packed)) gdt_ptr_t;

/* GDT segment selectors */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20

/* Load GDT and reload all segment registers (inline asm) */
static inline void gdt_flush(gdt_ptr_t *ptr)
{
    __asm__ volatile (
        "lgdt  (%0)          \n"
        "movw  $0x10, %%ax   \n"   /* kernel data selector */
        "movw  %%ax,  %%ds   \n"
        "movw  %%ax,  %%es   \n"
        "movw  %%ax,  %%fs   \n"
        "movw  %%ax,  %%gs   \n"
        "movw  %%ax,  %%ss   \n"
        /* far-return trick: push CS:EIP then lret to reload CS */
        "pushl $0x08         \n"
        "pushl $1f           \n"
        "lret                \n"
        "1:                  \n"
        :
        : "r"(ptr)
        : "eax", "memory"
    );
}

void gdt_init(void);

/* =========================================================================
 * IDT
 * ========================================================================= */

typedef struct {
    uint16_t offset_low;   /* bits 15:0  of handler address */
    uint16_t selector;     /* code segment selector         */
    uint8_t  zero;         /* always 0                      */
    uint8_t  type_attr;    /* P | DPL(2) | 0 | Type(4)     */
    uint16_t offset_high;  /* bits 31:16 of handler address */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;        /* size of IDT - 1 */
    uint32_t base;         /* linear address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* IDT gate type_attr flags */
#define IDT_GATE_INT32   0x8E  /* P=1, DPL=0, type=interrupt gate (32-bit) */
#define IDT_GATE_TRAP32  0x8F  /* P=1, DPL=0, type=trap gate     (32-bit) */
#define IDT_GATE_USER    0xEE  /* P=1, DPL=3, type=interrupt gate (32-bit) */

/* Load IDT (inline asm) */
static inline void idt_flush(idt_ptr_t *ptr)
{
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags);

/* =========================================================================
 * Paging
 * ========================================================================= */

typedef uint32_t pde_t;   /* page directory entry */
typedef uint32_t pte_t;   /* page table entry     */

/* Page flags */
#define PAGE_PRESENT   (1u << 0)
#define PAGE_WRITE     (1u << 1)
#define PAGE_USER      (1u << 2)
#define PAGE_PWT       (1u << 3)  /* write-through */
#define PAGE_PCD       (1u << 4)  /* cache disable */
#define PAGE_SIZE_4MB  (1u << 7)  /* PSE – only valid in PDE */

/* Map a single 4 KB page: virt → phys in page_dir */
void map_page(pde_t *page_dir, uint32_t virt, uint32_t phys, uint32_t flags);

#endif /* CPU_H */
