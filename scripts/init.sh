#!/usr/bin/env bash
# scripts/init.sh – Create disk image, partition, format FAT32, install GRUB
# Run once (or to reset the image from scratch).
# Requires: grub-pc-bin, grub-common, dosfstools, parted, losetup, mount
# Must be run as root (or via sudo).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
IMAGE="$BUILD_DIR/disk.img"
MOUNT_DIR="/mnt/scepter"
IMAGE_SIZE_MB=64

# ── Dependency check ────────────────────────────────────────────────────────
for cmd in dd parted losetup mkfs.fat grub-install mount umount; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' not found. Install missing packages:" >&2
        echo "  sudo apt install grub-pc-bin grub-common dosfstools parted util-linux" >&2
        exit 1
    fi
done

# ── Create build dir ─────────────────────────────────────────────────────────
mkdir -p "$BUILD_DIR"

# ── Create blank image ───────────────────────────────────────────────────────
echo "[init] Creating ${IMAGE_SIZE_MB} MB disk image: $IMAGE"
dd if=/dev/zero of="$IMAGE" bs=1M count="$IMAGE_SIZE_MB" status=progress

# ── Partition: MBR, one primary FAT32 partition (1MiB–100%), bootable ────────
echo "[init] Partitioning..."
parted -s "$IMAGE" mklabel msdos
parted -s "$IMAGE" mkpart primary fat32 1MiB 100%
parted -s "$IMAGE" set 1 boot on

# ── Attach loop device ───────────────────────────────────────────────────────
echo "[init] Attaching loop device..."
LOOP=$(losetup -fP --show "$IMAGE")
echo "[init] Loop device: $LOOP"

cleanup() {
    echo "[init] Cleaning up..."
    umount "$MOUNT_DIR" 2>/dev/null || true
    losetup -d "$LOOP"  2>/dev/null || true
}
trap cleanup EXIT

# ── Format partition as FAT32 ────────────────────────────────────────────────
echo "[init] Formatting ${LOOP}p1 as FAT32..."
mkfs.fat -F32 "${LOOP}p1"

# ── Mount and create directory structure ─────────────────────────────────────
echo "[init] Mounting..."
mkdir -p "$MOUNT_DIR"
mount "${LOOP}p1" "$MOUNT_DIR"
mkdir -p "$MOUNT_DIR/boot/grub"

# ── Install GRUB ─────────────────────────────────────────────────────────────
echo "[init] Installing GRUB..."
grub-install \
    --target=i386-pc \
    --boot-directory="$MOUNT_DIR/boot" \
    --no-floppy \
    "$LOOP"

# ── Write grub.cfg ───────────────────────────────────────────────────────────
echo "[init] Writing grub.cfg..."
cat > "$MOUNT_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=3
set default=0

menuentry "Scepter OS" {
    multiboot /boot/kernel.bin
    boot
}
EOF

# ── Copy kernel if already built ─────────────────────────────────────────────
KERNEL="$BUILD_DIR/kernel.bin"
if [ -f "$KERNEL" ]; then
    echo "[init] Copying kernel.bin..."
    cp "$KERNEL" "$MOUNT_DIR/boot/kernel.bin"
else
    echo "[init] kernel.bin not found – run 'make install' after building."
fi

# ── Sync and unmount ─────────────────────────────────────────────────────────
sync
umount "$MOUNT_DIR"
losetup -d "$LOOP"
trap - EXIT

chmod 777 "$IMAGE"
echo "[init] Done. Disk image ready: $IMAGE"
