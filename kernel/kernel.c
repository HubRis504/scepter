#include "cpu.h"
#include "vga.h"
#include "printk.h"
#include "pic.h"

void kernel_main() {
    gdt_init();
    idt_init();
    pic_init(0x20, 0x28);
    vga_init();

    printk("Hello, Kernel World!\n");

    while(1);
}
