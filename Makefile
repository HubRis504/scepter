AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -I include -I kernel -ggdb
LIBGCC  = $(shell i686-linux-gnu-gcc -print-libgcc-file-name)
LDFLAGS = -T linker.ld -nostdlib

BUILD      = build
TARGET     = $(BUILD)/kernel.bin
IMAGE      = $(BUILD)/disk.img
MOUNT_DIR  = /mnt/scepter

OBJS    = $(BUILD)/boot.o \
          $(BUILD)/kernel.o \
          $(BUILD)/cpu.o \
          $(BUILD)/printk.o \
          $(BUILD)/vga.o \
          $(BUILD)/pic.o \
          $(BUILD)/isr.o \
          $(BUILD)/panic.o \
          $(BUILD)/pit.o

.PHONY: all clean init mount umount install run debug

# ── Build ────────────────────────────────────────────────────────────────────

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

$(BUILD)/pic.o: driver/pic.c include/pic.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/pic.c -o $@

$(BUILD)/isr.o: kernel/isr.s
	$(AS) kernel/isr.s -o $@

$(BUILD)/panic.o: kernel/panic.c include/panic.h include/printk.h kernel/asm.h
	$(CC) $(CFLAGS) -c kernel/panic.c -o $@

$(BUILD)/pit.o: driver/pit.c include/pit.h include/pic.h include/cpu.h kernel/asm.h
	$(CC) $(CFLAGS) -c driver/pit.c -o $@

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBGCC)

# ── Disk image management ────────────────────────────────────────────────────

# One-time: create disk image, partition, format FAT32, install GRUB
init:
	sudo bash scripts/init.sh

# Mount the image partition at /mnt/scepter
mount:
	sudo bash scripts/mount.sh

# Unmount and detach loop device
umount:
	sudo bash scripts/umount.sh

# Copy freshly built kernel into the mounted image and flush to disk
install: $(TARGET)
	sudo cp $(TARGET) $(MOUNT_DIR)/boot/kernel.bin
	sudo sync
	@echo "[install] kernel.bin deployed to $(MOUNT_DIR)/boot/"

# ── QEMU ─────────────────────────────────────────────────────────────────────

# Boot from disk image (image must be init'd; kernel must be install'd)
run:
	qemu-system-i386 -drive file=$(IMAGE),format=raw

# Debug: start QEMU suspended, attach GDB
debug:
	qemu-system-i386 -drive file=$(IMAGE),format=raw -s -S &
	gdb $(TARGET) -ex "target remote :1234"

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD)
