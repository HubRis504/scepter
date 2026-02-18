#!/usr/bin/env bash
# scripts/umount.sh – Unmount FAT32 partition and detach loop device.
# Must be run as root (or via sudo).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
LOOPDEV_FILE="$BUILD_DIR/.loopdev"
MOUNT_DIR="/mnt/scepter"

# ── Sanity check ─────────────────────────────────────────────────────────────
if [ ! -f "$LOOPDEV_FILE" ]; then
    echo "ERROR: $LOOPDEV_FILE not found – image may not be mounted." >&2
    exit 1
fi

LOOP=$(cat "$LOOPDEV_FILE")

# ── Sync and unmount ─────────────────────────────────────────────────────────
echo "[umount] Syncing..."
sync

echo "[umount] Unmounting $MOUNT_DIR..."
umount "$MOUNT_DIR"

echo "[umount] Detaching loop device $LOOP..."
losetup -d "$LOOP"

rm -f "$LOOPDEV_FILE"
echo "[umount] Done."
