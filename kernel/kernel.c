#include "cpu.h"
#include "vga.h"
#include "printk.h"

void kernel_main() {
    gdt_init();
    idt_init();
    vga_init();

    printk("Hello, Kernel World!\n");

    while(1);
}
