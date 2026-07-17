// log_rotate.c
// In-place log rotation (overwrites oldest file, no extra space needed)

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
//#include <time.h>
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "log_rotate.h"
#include "project_defs.h"
#include "system_config.h"
#include "log_levels.h"
//#include "ntp_client.h"

// ============================================================
// Static Variables
// ============================================================
static const char *LOG_BASE_PATH = "/spiffs/logs/";
static const char *LOG_FILE_NAMES[WQMS_LOG_TYPE_MAX] = {
    "system_",      // system_0.log, system_1.log, ...
    "system_",      // application_0.log, ...
    "system_",      // automation_0.log, ...
    "system_",      // notification_0.log, ...
    "system_",      // integration_0.log, ...
    "system_",      // sensor_0.log, ...
    "system_",      // relay_0.log, ...
    "system_"       // api_0.log, ...
};
static const char *LOG_FILE_EXT = ".log";

// ============================================================
// Internal Functions
// ============================================================

// Build full file path
static void build_file_path(wqms_log_type_t type, int index, char *buffer, size_t size) {
    snprintf(buffer, size, "%s%s%d%s", 
             LOG_BASE_PATH, 
             LOG_FILE_NAMES[type], 
             index, 
             LOG_FILE_EXT);
}

// Get file size
static long get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// ============================================================
// Public Functions
// ============================================================

// Get filename for a specific log file index
const char* log_rotate_get_filename(wqms_log_type_t type, int index) {
    static char path[64];
    build_file_path(type, index, path, sizeof(path));
    return path;
}

// Open the next log file (creates directory if needed)
FILE* log_rotate_open(wqms_log_type_t type) {
    // 1. Ensure directory exists
    mkdir("/spiffs/logs", 0777);
    
    // 2. Find the next file to write (circular)
    char path[64];
    long size = -1;

    // Find the file to append
    for (int i = 0; i < LOG_MAX_FILES; i++) {
        build_file_path(type, i, path, sizeof(path));
        size = get_file_size(path);
        if (size < LOG_FILE_SIZE) {
            if ((i+1) < LOG_MAX_FILES){
                build_file_path(type, i + 1, path, sizeof(path));
                if (get_file_size(path) == -1) {
                    build_file_path(type, i, path, sizeof(path));
                    break;
                }
            }
        }
    }

    if (size >= LOG_FILE_SIZE) {
        build_file_path(type, 0, path, sizeof(path));
        unlink(path);
        for (int i = 1; i < LOG_MAX_FILES; i++) {
            char newpath[64];
            build_file_path(type, i, path, sizeof(path));
            build_file_path(type, i - 1, newpath, sizeof(newpath));
            rename(path, newpath);
        }
    }

    // 3. Open file
    FILE *file = fopen(path, "a");
    if (file == NULL) {
        WQMS_LOG_E("Failed to open log file: %s", path);
        return NULL;
    }

    // 4. Write header
//    char ts_str[32];
//    time_t ts = ntp_get_time();
//    struct tm *tm_info = localtime(&ts);
//    strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", tm_info);
//    fprintf(file, "--- Log started at %s ---\n", ts_str);
//    fflush(file);

    WQMS_LOG_D("Opened log file: %s", path);
    return file;
}

// Check if rotation is needed
int log_rotate_check(FILE *file, wqms_log_type_t type) {
    if (file == NULL) return 0;
    
    long current_pos = ftell(file);
    if (current_pos < 0) return 0;
    
    // Rotate if file exceeds max size
    if (current_pos > LOG_FILE_SIZE) {
        WQMS_LOG_D("Log file exceeded size, rotating");
        return 1;
    }
    
    return 0;
}

// Close the current log file
void log_rotate_close(wqms_log_type_t type) {
    WQMS_LOG_D("Closed log file for type %d", type);
}
