AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -I include -I kernel
LIBGCC  = $(shell i686-linux-gnu-gcc -print-libgcc-file-name)
LDFLAGS = -T linker.ld -nostdlib

BUILD   = build
TARGET  = $(BUILD)/kernel.bin

OBJS    = $(BUILD)/boot.o \
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
          $(BUILD)/driver.o

.PHONY: all clean run debug

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

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

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBGCC)

run: $(TARGET)
	qemu-system-i386 -m 4096 -kernel $(TARGET)

debug: $(TARGET)
	qemu-system-i386 -m 4096 -kernel $(TARGET) -s -S &
	gdb $(TARGET) -ex "target remote :1234"

clean:
	rm -rf $(BUILD)
