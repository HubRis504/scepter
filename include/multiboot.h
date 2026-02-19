#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* =========================================================================
 * Multiboot 1 specification structures
 * Ref: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 * ========================================================================= */

/* Magic value placed in EAX by the bootloader */
#define MB_MAGIC        0x2BADB002U

/* Flags in multiboot_info_t.flags */
#define MB_FLAG_MEM     (1u << 0)   /* mem_lower / mem_upper valid  */
#define MB_FLAG_MMAP    (1u << 6)   /* mmap_length / mmap_addr valid */

/* mmap entry type values */
#define MB_MMAP_AVAILABLE  1        /* usable RAM */
#define MB_MMAP_RESERVED   2        /* reserved / unusable */

/* -------------------------------------------------------------------------
 * Memory-map entry (pointed to by mmap_addr)
 * Note: the 'size' field is NOT included in 'size' itself.
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t size;          /* size of this entry (not counting this field) */
    uint64_t base_addr;     /* start of memory region                       */
    uint64_t length;        /* length of memory region in bytes             */
    uint32_t type;          /* MB_MMAP_AVAILABLE = 1, else reserved         */
} __attribute__((packed)) mb_mmap_entry_t;

/* -------------------------------------------------------------------------
 * Main Multiboot information structure passed in EBX by the bootloader
 * Only the fields we actually use are listed; the struct is padded to the
 * correct offsets so it is safe to cast the raw EBX pointer.
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t flags;         /* which fields below are valid                 */
    uint32_t mem_lower;     /* KB of lower memory  (valid if MB_FLAG_MEM)  */
    uint32_t mem_upper;     /* KB of upper memory  (valid if MB_FLAG_MEM)  */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;   /* bytes in mmap table (valid if MB_FLAG_MMAP) */
    uint32_t mmap_addr;     /* physical address of first mb_mmap_entry_t   */
    /* ... further fields exist but are not needed here */
} __attribute__((packed)) multiboot_info_t;

#endif /* MULTIBOOT_H */
