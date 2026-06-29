// log_rotate.c
// In-place log rotation (overwrites oldest file, no extra space needed)

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "log_rotate.h"
#include "project_defs.h"
#include "system_config.h"
#include "log_levels.h"

// ============================================================
// Static Variables
// ============================================================
static const char *LOG_BASE_PATH = "/spiffs/logs/";
static const char *LOG_FILE_NAMES[WQMS_LOG_TYPE_MAX] = {
    "system_",      // system_0.log, system_1.log, ...
    "application_", // application_0.log, ...
    "automation_"   // automation_0.log, ...
};
static const char *LOG_FILE_EXT = ".log";

static int current_index[WQMS_LOG_TYPE_MAX] = {0, 0, 0};

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
    int index = 0;
    char path[64];
    long smallest_size = -1;
    int smallest_index = 0;
    
    // Find smallest file (oldest) to overwrite
    for (int i = 0; i < LOG_MAX_FILES; i++) {
        build_file_path(type, i, path, sizeof(path));
        long size = get_file_size(path);
        if (size == -1) {
            // File doesn't exist - use this one
            index = i;
            break;
        }
        if (smallest_size == -1 || size < smallest_size) {
            smallest_size = size;
            smallest_index = i;
        }
    }
    
    // If all files exist, overwrite the smallest (oldest)
    index = smallest_index;
    current_index[type] = index;
    
    // 3. Open file (overwrite if exists)
    build_file_path(type, index, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        WQMS_LOG_E("Failed to open log file: %s", path);
        return NULL;
    }
    
    // 4. Write header
    fprintf(file, "--- Log started at %s ---\n", "");
    fflush(file);
    
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
