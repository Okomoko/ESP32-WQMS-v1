// littlefs_manager.h
// LITTLEFS mount and file management

#ifndef LITTLEFS_MANAGER_H
#define LITTLEFS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// ============================================================
// Function Prototypes
// ============================================================

// Initialize LITTLEFS partitions (logs + sensors)
void littlefs_init(void);

// Mount a specific LITTLEFS partition
int littlefs_mount(const char *partition_name, const char *mount_point);

// Unmount a LITTLEFS partition
int littlefs_unmount(const char *mount_point);

// Check if a file exists
int littlefs_file_exists(const char *path);

// Get file size
int littlefs_file_size(const char *path);

// Delete a file
int littlefs_file_delete(const char *path);

// List files in a directory
int littlefs_list_files(const char *path, char **files, int max_files);

// Get free space on a mount point
uint32_t littlefs_free_space(const char *mount_point);

// Get total space on a mount point
uint32_t littlefs_total_space(const char *mount_point);

// Format a specific LITTLEFS partition
esp_err_t format_littlefs(const char *partition_label);

#endif // LITTLEFS_MANAGER_H