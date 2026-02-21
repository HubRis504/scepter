#!/bin/bash
#
# Create a clean bootable GRUB disk image with FAT32
# Usage: sudo ./script/make_grub_disk.sh
#

set -e  # Exit on error

DISK_IMG="disk.img"
DISK_SIZE_MB=128  # Increased for FAT32 (was 64)

echo "=================================================="
echo "Creating Clean Bootable GRUB Disk Image (FAT32)"
echo "=================================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)"
    exit 1
fi

# Step 1: Create blank disk image
echo "[1/7] Creating ${DISK_SIZE_MB}MB disk image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=$DISK_SIZE_MB status=progress

# Step 2: Create partition table with fdisk
echo "[2/7] Creating MBR partition table..."
fdisk "$DISK_IMG" << EOF > /dev/null 2>&1
o
n
p
1
2048

t
c
a
w
EOF

# Step 3: Setup loop device
echo "[3/7] Setting up loop device..."
LOOP_DEV=$(losetup -f)
losetup -P "$LOOP_DEV" "$DISK_IMG"
echo "    Loop device: $LOOP_DEV"

# Wait for partition to appear
sleep 1

# Step 4: Format partition as FAT32
echo "[4/7] Formatting partition as FAT32..."
mkfs.vfat -F 32 -n "SCEPTER" "${LOOP_DEV}p1" > /dev/null 2>&1

# Step 5: Mount partition temporarily
echo "[5/7] Mounting partition..."
TEMP_MOUNT=$(mktemp -d)
mount "${LOOP_DEV}p1" "$TEMP_MOUNT"

# Step 6: Install GRUB
echo "[6/7] Installing GRUB bootloader..."
grub-install --target=i386-pc --boot-directory="$TEMP_MOUNT/boot" "$LOOP_DEV" 2>&1 | grep -v "Installing"

# Step 7: Create grub.cfg
echo "[7/7] Creating GRUB configuration..."
mkdir -p "$TEMP_MOUNT/boot/grub"
cat > "$TEMP_MOUNT/boot/grub/grub.cfg" << 'EOF'
set timeout=5
set default=0

menuentry "Scepter i386 Kernel" {
    multiboot /boot/kernel.elf
    boot
}
EOF

# Cleanup
umount "$TEMP_MOUNT"
rmdir "$TEMP_MOUNT"
losetup -d "$LOOP_DEV"
chmod 666 "$DISK_IMG"

echo ""
echo "=================================================="
echo "✓ Clean GRUB disk created: $DISK_IMG (FAT32)"
echo "=================================================="
echo ""
echo "Partition layout:"
echo "  - Partition 1: FAT32, bootable, ~128MB"
echo "  - Volume label: SCEPTER"
echo ""
echo "Use 'make mount' to mount the disk for kernel updates"
echo ""