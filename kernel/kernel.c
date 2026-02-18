#include "cpu.h"
#include "vga.h"
#include "printk.h"
#include "pic.h"
#include "pit.h"

void kernel_main() {
    gdt_init();
    idt_init();
    isr_init();
    pic_init(0x20, 0x28);
    vga_init();
    pit_init(100);
    sti();

    printk("Hello, Kernel World!\n");

    while(1);
}
