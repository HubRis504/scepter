#!/usr/bin/env bash
# scripts/mount.sh – Attach disk image as loop device and mount FAT32 partition.
# Must be run as root (or via sudo).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
IMAGE="$BUILD_DIR/disk.img"
LOOPDEV_FILE="$BUILD_DIR/.loopdev"
MOUNT_DIR="/mnt/scepter"

# ── Sanity checks ────────────────────────────────────────────────────────────
if [ ! -f "$IMAGE" ]; then
    echo "ERROR: $IMAGE not found. Run 'make init' first." >&2
    exit 1
fi

if [ -f "$LOOPDEV_FILE" ]; then
    EXISTING=$(cat "$LOOPDEV_FILE")
    echo "WARNING: Image appears already mounted (loop: $EXISTING)." >&2
    echo "         Run 'make umount' first if this is stale." >&2
    exit 1
fi

# ── Attach loop device ───────────────────────────────────────────────────────
echo "[mount] Attaching loop device..."
LOOP=$(losetup -fP --show "$IMAGE")
echo "$LOOP" > "$LOOPDEV_FILE"
echo "[mount] Loop device: $LOOP"

# ── Mount partition ──────────────────────────────────────────────────────────
mkdir -p "$MOUNT_DIR"
mount "${LOOP}p1" "$MOUNT_DIR"
echo "[mount] Mounted ${LOOP}p1 at $MOUNT_DIR"
