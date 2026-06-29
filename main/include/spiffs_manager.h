// spiffs_manager.h
// SPIFFS mount and file management

#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

// ============================================================
// Function Prototypes
// ============================================================

// Initialize SPIFFS partitions (logs + sensors)
void spiffs_init(void);

// Mount a specific SPIFFS partition
int spiffs_mount(const char *partition_name, const char *mount_point);

// Unmount a SPIFFS partition
int spiffs_unmount(const char *mount_point);

// Check if a file exists
int spiffs_file_exists(const char *path);

// Get file size
int spiffs_file_size(const char *path);

// Delete a file
int spiffs_file_delete(const char *path);

// List files in a directory
int spiffs_list_files(const char *path, char **files, int max_files);

// Get free space on a mount point
uint32_t spiffs_free_space(const char *mount_point);

// Get total space on a mount point
uint32_t spiffs_total_space(const char *mount_point);

#endif // SPIFFS_MANAGER_H