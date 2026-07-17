// spiffs_manager.c
// SPIFFS mount and file management - only logs and sensors

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_vfs.h"

#include "spiffs_manager.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Static Variables
// ============================================================
static int spiffs_logs_mounted = 0;
static int spiffs_sensors_mounted = 0;

// ============================================================
// Internal Functions
// ============================================================
static esp_err_t mount_spiffs(const char *partition, const char *mount_point, int *mounted_flag) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition,
        .max_files = 10,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            WQMS_LOG_E("Failed to mount SPIFFS partition: %s", partition);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            WQMS_LOG_E("SPIFFS partition not found: %s", partition);
        } else {
            WQMS_LOG_E("SPIFFS mount error: %s (%d)", partition, ret);
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(partition, &total, &used);
    if (ret == ESP_OK) {
        WQMS_LOG_D("SPIFFS %s mounted: total=%lu, used=%lu", partition, total, used);
        *mounted_flag = 1;
    }
    
    return ret;
}

esp_err_t format_spiffs(const char *partition_label) {
    return esp_spiffs_format(partition_label);
}

// ============================================================
// Public Functions
// ============================================================

void spiffs_init(void) {
    // Mount logs partition
    mount_spiffs("logs", "/spiffs/logs", &spiffs_logs_mounted);
    
    // Mount sensors partition
    mount_spiffs("sensors", "/spiffs/sensors", &spiffs_sensors_mounted);
    
    // Create directories if needed
    if (spiffs_logs_mounted) {
        mkdir("/spiffs/logs", 0777);
        WQMS_LOG_D("Logs directory ready");
    }
    if (spiffs_sensors_mounted) {
        mkdir("/spiffs/sensors", 0777);
        WQMS_LOG_D("Sensors directory ready");
    }
    
    WQMS_LOG_I("SPIFFS initialized (logs + sensors)");
}

int spiffs_mount(const char *partition_name, const char *mount_point) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_name,
        .max_files = 10,
        .format_if_mount_failed = true
    };
    return esp_vfs_spiffs_register(&conf);
}

int spiffs_unmount(const char *mount_point) {
    return esp_vfs_spiffs_unregister(mount_point);
}

int spiffs_file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

int spiffs_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

int spiffs_file_delete(const char *path) {
    return (unlink(path) == 0) ? 0 : -1;
}

int spiffs_list_files(const char *path, char **files, int max_files) {
    DIR *dir = opendir(path);
    if (!dir) return -1;
    
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        if (entry->d_type == DT_REG) {
            files[count] = strdup(entry->d_name);
            count++;
        }
    }
    closedir(dir);
    return count;
}

uint32_t spiffs_free_space(const char *mount_point) {
    size_t total = 0, used = 0;
    if (esp_spiffs_info(NULL, &total, &used) == ESP_OK) {
        return total - used;
    }
    return 0;
}

uint32_t spiffs_total_space(const char *mount_point) {
    size_t total = 0, used = 0;
    if (esp_spiffs_info(NULL, &total, &used) == ESP_OK) {
        return total;
    }
    return 0;
}