AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
OBJCOPY = i686-linux-gnu-objcopy
NASM    = nasm
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -I include -I kernel
LIBGCC  = $(shell i686-linux-gnu-gcc -print-libgcc-file-name)
LDFLAGS = -T linker.ld -nostdlib

BUILD   = build
BOOTLOADER = $(BUILD)/bootloader.bin
KERNEL_ELF = $(BUILD)/kernel.elf
KERNEL_BIN = $(BUILD)/kernel.bin
DISK_IMG   = $(BUILD)/scepter.img

OBJS    = $(BUILD)/header.o \
          $(BUILD)/boot.o \
          $(BUILD)/kernel.o \
          $(BUILD)/cpu.o \
          $(BUILD)/printk.o \
          $(BUILD)/vga.o \
          $(BUILD)/pic.o \
          $(BUILD)/isr.o \
          $(BUILD)/panic.o \
          $(BUILD)/pit.o \
          $(BUILD)/buddy.o \
          $(BUILD)/slab.o \
          $(BUILD)/driver.o \
          $(BUILD)/tty.o

.PHONY: all clean run debug disk

all: $(BUILD) $(DISK_IMG)

disk: $(DISK_IMG)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/header.o: kernel/header.s
	$(AS) kernel/header.s -o $@

$(BUILD)/boot.o: kernel/boot.s
	$(AS) kernel/boot.s -o $@

$(BUILD)/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c kernel/kernel.c -o $@

$(BUILD)/cpu.o: kernel/cpu.c include/cpu.h kernel/asm.h
	$(CC) $(CFLAGS) -c kernel/cpu.c -o $@

$(BUILD)/printk.o: kernel/printk.c include/printk.h include/vga.h
	$(CC) $(CFLAGS) -c kernel/printk.c -o $@

$(BUILD)/vga.o: driver/char/vga.c include/vga.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/char/vga.c -o $@

$(BUILD)/driver.o: driver/driver.c include/driver.h include/slab.h
	$(CC) $(CFLAGS) -c driver/driver.c -o $@

$(BUILD)/tty.o: driver/char/tty.c include/tty.h include/vga.h include/driver.h
	$(CC) $(CFLAGS) -c driver/char/tty.c -o $@

$(BUILD)/pic.o: driver/pic.c include/pic.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/pic.c -o $@

$(BUILD)/isr.o: kernel/isr.s
	$(AS) kernel/isr.s -o $@

$(BUILD)/panic.o: kernel/panic.c include/panic.h include/printk.h kernel/asm.h
	$(CC) $(CFLAGS) -c kernel/panic.c -o $@

$(BUILD)/pit.o: driver/char/pit.c include/pit.h include/pic.h include/cpu.h include/driver.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/char/pit.c -o $@

$(BUILD)/buddy.o: mm/buddy.c include/buddy.h include/printk.h
	$(CC) $(CFLAGS) -c mm/buddy.c -o $@

$(BUILD)/slab.o: mm/slab.c include/slab.h include/buddy.h include/printk.h
	$(CC) $(CFLAGS) -c mm/slab.c -o $@

# Build bootloader
$(BOOTLOADER): boot/bootloader.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

# Build kernel ELF
$(KERNEL_ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBGCC)

# Convert kernel ELF to flat binary
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# Create bootable disk image
$(DISK_IMG): $(BOOTLOADER) $(KERNEL_BIN)
	cat $(BOOTLOADER) $(KERNEL_BIN) > $@
	@echo "Disk image created: $(DISK_IMG)"
	@echo "Bootloader size: $$(stat -c%s $(BOOTLOADER)) bytes"
	@echo "Kernel size: $$(stat -c%s $(KERNEL_BIN)) bytes"
	@echo "Total image size: $$(stat -c%s $(DISK_IMG)) bytes"

run: $(DISK_IMG)
	qemu-system-i386 -m 4096 -drive file=$(DISK_IMG),format=raw,if=ide

debug: $(DISK_IMG)
	qemu-system-i386 -m 4096 -drive file=$(DISK_IMG),format=raw,if=ide -s -S &
	gdb $(KERNEL_ELF) -ex "target remote :1234"

clean:
	rm -rf $(BUILD)
