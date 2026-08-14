// littlefs_manager.c
// LittleFS mount and file management - only logs and sensors

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "esp_vfs.h"

#include "littlefs_manager.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Global Variables - Definition
// ============================================================
int littlefs_logs_mounted = 0;
int littlefs_sensors_mounted = 0;
int littlefs_web_assets_mounted = 0;

// ============================================================
// Internal Functions
// ============================================================
static esp_err_t mount_littlefs(const char *partition, const char *mount_point, int *mounted_flag) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            WQMS_LOG_E("Failed to mount LittleFS partition: %s", partition);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            WQMS_LOG_E("LittleFS partition not found: %s", partition);
        } else {
            WQMS_LOG_E("LittleFS mount error: %s (%d)", partition, ret);
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(partition, &total, &used);
    if (ret == ESP_OK) {
        WQMS_LOG_D("LittleFS %s mounted: total=%lu, used=%lu", partition, total, used);
        *mounted_flag = 1;
        WQMS_LOG_D("LittleFS %s mounted successfully.", partition);
    }
    
    return ret;
}

esp_err_t format_littlefs(const char *partition_label) {
    return esp_littlefs_format(partition_label);
}

// ============================================================
// Public Functions
// ============================================================

void littlefs_init(void) {
    // Mount logs partition
    esp_err_t ret = mount_littlefs(LOG_PARTITION_NAME, LOG_BASE_PATH, &littlefs_logs_mounted);
    WQMS_LOG_D("%s, mounted : %d", LOG_PARTITION_NAME, ret);
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (littlefs_logs_mounted) {
        ret = mkdir(LOG_BASE_PATH, 0777);
        WQMS_LOG_D("%s, created : %d", LOG_BASE_PATH, ret);
    }
    // Mount sensors partition
    ret = mount_littlefs(SENSOR_PARTITION_NAME, SENSOR_BASE_PATH, &littlefs_sensors_mounted);
    vTaskDelay(pdMS_TO_TICKS(2000));
    WQMS_LOG_D("%s, mounted : %d", SENSOR_PARTITION_NAME, ret);
    if (littlefs_sensors_mounted) {
        ret = mkdir(SENSOR_BASE_PATH, 0777);
        WQMS_LOG_D("%s, created : %d", SENSOR_BASE_PATH, ret);
    }
    // Mount web assets partition
    ret = mount_littlefs(WEB_PARTITION_NAME, WEB_BASE_PATH, &littlefs_web_assets_mounted);
    vTaskDelay(pdMS_TO_TICKS(2000));
    WQMS_LOG_D("%s, mounted : %d", WEB_PARTITION_NAME, ret);
    if (littlefs_web_assets_mounted) {
        ret = mkdir(WEB_BASE_PATH, 0777);
        WQMS_LOG_D("%s, created : %d", WEB_BASE_PATH, ret);
    }

    WQMS_LOG_I("LittleFS initialized.");
}

int littlefs_mount(const char *partition_name, const char *mount_point) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_name,
        .format_if_mount_failed = true
    };
    return esp_vfs_littlefs_register(&conf);
}

int littlefs_unmount(const char *mount_point) {
    return esp_vfs_littlefs_unregister(mount_point);
}

int littlefs_file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

int littlefs_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

int littlefs_file_delete(const char *path) {
    return (unlink(path) == 0) ? 0 : -1;
}

int littlefs_list_files(const char *path, char **files, int max_files) {
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

uint32_t littlefs_free_space(const char *mount_point) {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(NULL, &total, &used) == ESP_OK) {
        return total - used;
    }
    return 0;
}

uint32_t littlefs_total_space(const char *mount_point) {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(NULL, &total, &used) == ESP_OK) {
        return total;
    }
    return 0;
}