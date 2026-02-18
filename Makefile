AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -I include -I kernel
LDFLAGS = -T linker.ld -nostdlib

BUILD   = build
TARGET  = $(BUILD)/kernel.bin

OBJS    = $(BUILD)/boot.o \
          $(BUILD)/kernel.o \
          $(BUILD)/cpu.o \
          $(BUILD)/printk.o \
          $(BUILD)/vga.o

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

$(BUILD)/vga.o: driver/vga.c include/vga.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/vga.c -o $@

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: $(TARGET)
	qemu-system-i386 -kernel $(TARGET)

debug: $(TARGET)
	qemu-system-i386 -kernel $(TARGET) -s -S &
	gdb $(TARGET) -ex "target remote :1234"

clean:
	rm -rf $(BUILD)
