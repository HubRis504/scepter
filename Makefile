AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
OBJCOPY = i686-linux-gnu-objcopy

CFLAGS  = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -mno-red-zone -O2 -Wall -Wextra -I include -I kernel \
          -fno-pic -m32
LIBGCC  = $(shell i686-linux-gnu-gcc -print-libgcc-file-name)
LDFLAGS = -T linker.ld -nostdlib -Map=kernel.sym

BUILD   = build
TARGET  = $(BUILD)/kernel.elf

KERNEL_OBJS = $(BUILD)/boot.o \
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
              $(BUILD)/tty.o \
              $(BUILD)/ide.o

.PHONY: all clean run debug

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

# ===========================================================================
# Kernel Build
# ===========================================================================
$(BUILD)/boot.o: kernel/boot.s
	$(AS) kernel/boot.s -o $@

$(BUILD)/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) kernel/kernel.c -o $@

$(BUILD)/cpu.o: kernel/cpu.c include/cpu.h kernel/asm.h
	$(CC) $(CFLAGS) kernel/cpu.c -o $@

$(BUILD)/printk.o: kernel/printk.c include/printk.h include/vga.h
	$(CC) $(CFLAGS) kernel/printk.c -o $@

$(BUILD)/vga.o: driver/char/vga.c include/vga.h kernel/asm.h
	$(CC) $(CFLAGS) driver/char/vga.c -o $@

$(BUILD)/driver.o: driver/driver.c include/driver.h include/slab.h
	$(CC) $(CFLAGS) driver/driver.c -o $@

$(BUILD)/tty.o: driver/char/tty.c include/tty.h include/vga.h include/driver.h
	$(CC) $(CFLAGS) driver/char/tty.c -o $@

$(BUILD)/pic.o: driver/pic.c include/pic.h kernel/asm.h
	$(CC) $(CFLAGS) driver/pic.c -o $@

$(BUILD)/isr.o: kernel/isr.s
	$(AS) kernel/isr.s -o $@

$(BUILD)/panic.o: kernel/panic.c include/panic.h include/printk.h kernel/asm.h
	$(CC) $(CFLAGS) kernel/panic.c -o $@

$(BUILD)/pit.o: driver/char/pit.c include/pit.h include/pic.h include/cpu.h include/driver.h kernel/asm.h
	$(CC) $(CFLAGS) driver/char/pit.c -o $@

$(BUILD)/buddy.o: mm/buddy.c include/buddy.h include/printk.h
	$(CC) $(CFLAGS) mm/buddy.c -o $@

$(BUILD)/slab.o: mm/slab.c include/slab.h include/buddy.h include/printk.h
	$(CC) $(CFLAGS) mm/slab.c -o $@

$(BUILD)/ide.o: driver/block/ide.c include/ide.h kernel/asm.h include/printk.h
	$(CC) $(CFLAGS) driver/block/ide.c -o $@

# Link kernel as ELF (Multiboot compatible)
$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS) $(LIBGCC)
	@echo "Kernel ELF created: $@"
	@ls -lh $@

# ===========================================================================
# Run and Debug
# ===========================================================================
run: $(TARGET)
	qemu-system-i386 -m 128 -kernel $(TARGET) -hda disk.img

debug: $(TARGET)
	qemu-system-i386 -m 4096 -kernel $(TARGET) -s -S -hda disk.img &
	gdb -ex "target remote :1234" -ex "symbol-file $(TARGET)"

clean:
	rm -rf $(BUILD)