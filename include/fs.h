#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Virtual Filesystem (VFS) Abstraction Layer
 *
 * Provides unified interface for different filesystem types.
 * Features:
 * - Mount point management
 * - Path resolution
 * - Dynamic file handle allocation (linked list)
 * - Multiple filesystem driver support
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_MOUNT_POINTS  16
#define MAX_PATH_LEN      256
#define MAX_FS_NAME       32

/* File open flags */
#define O_RDONLY  0x0001
#define O_WRONLY  0x0002
#define O_RDWR    0x0003
#define O_CREAT   0x0100
#define O_APPEND  0x0200

/* Standard file descriptors (reserved) */
#define STDIN_FD   0
#define STDOUT_FD  1
#define STDERR_FD  2

/* -------------------------------------------------------------------------
 * Filesystem Driver Operations
 * ------------------------------------------------------------------------- */

typedef struct fs_ops {
    /**
     * Mount a filesystem
     * @param dev_id Block device ID
     * @param part_id Partition number
     * @param fs_private Output: filesystem-specific data pointer
     * @return 0 on success, -1 on error
     */
    int (*mount)(int dev_id, int part_id, void **fs_private);
    
    /**
     * Unmount a filesystem
     * @param fs_private Filesystem-specific data
     * @return 0 on success, -1 on error
     */
    int (*unmount)(void *fs_private);
    
    /**
     * Open a file
     * @param fs_private Filesystem-specific mount data
     * @param path Path relative to filesystem root
     * @param flags Open flags (O_RDONLY, etc.)
     * @param file_private Output: file-specific data pointer
     * @return 0 on success, -1 on error
     */
    int (*open)(void *fs_private, const char *path, int flags, void **file_private);
    
    /**
     * Close a file
     * @param file_private File-specific data
     * @return 0 on success, -1 on error
     */
    int (*close)(void *file_private);
    
    /**
     * Read from file
     * @param file_private File-specific data
     * @param buf Buffer to read into
     * @param count Bytes to read
     * @return Bytes read, or -1 on error
     */
    int (*read)(void *file_private, void *buf, size_t count);
    
    /**
     * Write to file
     * @param file_private File-specific data
     * @param buf Buffer to write from
     * @param count Bytes to write
     * @return Bytes written, or -1 on error
     */
    int (*write)(void *file_private, const void *buf, size_t count);
} fs_ops_t;

/* -------------------------------------------------------------------------
 * Filesystem Registration
 * ------------------------------------------------------------------------- */

/**
 * Register a filesystem driver
 * @param fs_name Filesystem type name (e.g., "ext2", "fat32")
 * @param ops Operations structure
 * @return Filesystem ID (>= 0), or -1 on error
 */
int register_filesystem(const char *fs_name, fs_ops_t *ops);

/* -------------------------------------------------------------------------
 * Mount Management
 * ------------------------------------------------------------------------- */

/**
 * Mount a filesystem
 * @param device_id Block device ID (e.g., 4 for hda partitions)
 * @param partition_id Partition number (1-4)
 * @param fs_type Filesystem type name
 * @param mount_path Where to mount (e.g., "/", "/mnt/data")
 * @return 0 on success, -1 on error
 */
int fs_mount(int device_id, int partition_id, const char *fs_type, const char *mount_path);

/**
 * Unmount a filesystem
 * @param mount_path Mount point to unmount
 * @return 0 on success, -1 on error
 */
int fs_unmount(const char *mount_path);

/* -------------------------------------------------------------------------
 * File Operations (Public API)
 * ------------------------------------------------------------------------- */

/**
 * Open a file
 * @param path Absolute path (e.g., "/etc/config.txt")
 * @param flags O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_APPEND
 * @return File descriptor (>= 3), or -1 on error
 */
int fs_open(const char *path, int flags);

/**
 * Close a file
 * @param fd File descriptor
 * @return 0 on success, -1 on error
 */
int fs_close(int fd);

/**
 * Read from file
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Bytes to read
 * @return Bytes read, or -1 on error
 */
int fs_read(int fd, void *buf, size_t count);

/**
 * Write to file
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Bytes to write
 * @return Bytes written, or -1 on error
 */
int fs_write(int fd, const void *buf, size_t count);

/* -------------------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------------------- */

/**
 * Initialize the VFS subsystem
 * Must be called before any filesystem operations
 */
void vfs_init(void);

#endif /* FS_H */