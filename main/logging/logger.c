// logger.c
// Core logging system with compression, rotation, and RS232 output

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "log_levels.h"
#include "logger.h"
#include "project_defs.h"
#include "system_config.h"
#include "log_rotate.h"

// ============================================================
// Static Variables
// ============================================================
static const char *log_type_names[] = {
    [WQMS_LOG_TYPE_SYSTEM] = "SYS",
    [WQMS_LOG_TYPE_APPLICATION] = "APPL",
    [WQMS_LOG_TYPE_AUTOMATION] = "AUTO",
    [WQMS_LOG_TYPE_NOTIFICATION] = "APPL",
    [WQMS_LOG_TYPE_INTEGRATION] = "APPL",
    [WQMS_LOG_TYPE_SENSOR] = "APPL",
    [WQMS_LOG_TYPE_RELAY] = "APPL",
    [WQMS_LOG_TYPE_API] = "APPL"
};

static const char *log_level_names[] = {
    [WQMS_LOG_LEVEL_ENUM_ERROR] = "ERROR",
    [WQMS_LOG_LEVEL_ENUM_WARN] = "WARN",
    [WQMS_LOG_LEVEL_ENUM_INFO] = "INFO",
    [WQMS_LOG_LEVEL_ENUM_DEBUG] = "DEBUG",
    [WQMS_LOG_LEVEL_ENUM_VERBOSE] = "VERB"
};

// Current log file handles
static FILE *log_files[WQMS_LOG_TYPE_MAX] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

// ============================================================
// Internal Functions
// ============================================================

// Get current timestamp as string
static void get_timestamp_str(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Write to RS232 console (if enabled)
static void wqms_rs232_output(const char *msg) {
    #ifdef CONFIG_RS232_CONSOLE_ENABLE
        uart_write_bytes(UART_NUM_0, msg, strlen(msg));
        uart_write_bytes(UART_NUM_0, "\r\n", 2);
    #else
        (void)msg;
    #endif
}

// ============================================================
// Public Function: Initialize Logging
// ============================================================
void log_init(void) {
    // Open all log files (create if not exist)
    for (int i = 0; i < WQMS_LOG_TYPE_MAX; i++) {
        log_files[i] = log_rotate_open((wqms_log_type_t)i);
        if (log_files[i] == NULL) {
            // Fallback to printf if file fails
            printf("Failed to open log file for type %d\n", i);
        }
    }

    WQMS_LOG_I("Logging system initialized");
    WQMS_RS232_PRINT("Logging system initialized");
}

// ============================================================
// Public Function: Write Log Entry
// ============================================================
void wqms_log_write(wqms_log_type_t type, wqms_log_level_t level, const char *format, ...) {
    // 1. Validate inputs
    if (type >= WQMS_LOG_TYPE_MAX) return;
    if (level < WQMS_LOG_LEVEL_ENUM_ERROR || level > WQMS_LOG_LEVEL_ENUM_VERBOSE) return;
    
    // 2. Format message with timestamp and level
    char timestamp[32];
    get_timestamp_str(timestamp, sizeof(timestamp));

    char msg_buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
    va_end(args);

    // 3. Build full log line
    char full_line[320];
    snprintf(full_line, sizeof(full_line),
             "[%s] [%s] [%s] %s",
             timestamp,
             log_type_names[type],
             log_level_names[level],
             msg_buffer);

    // 4. Write to log file (with rotation check)
    if (log_files[type] != NULL) {
        // Check if rotation needed (file size exceeded)
        if (log_rotate_check(log_files[type], type)) {
            log_rotate_close(type);
            log_files[type] = log_rotate_open(type);
        }
        
        // Write to file
        fprintf(log_files[type], "%s\n", full_line);
        fflush(log_files[type]);
    }

    // 5. Write to RS232 console (if enabled and level <= INFO)
    #ifdef CONFIG_RS232_CONSOLE_ENABLE
        wqms_rs232_output(full_line);
    #endif
    
    // 6. Also output to ESP_LOG for IDF console
    if (level >= WQMS_LOG_LEVEL_ENUM_INFO) {
        ESP_LOGI("WQMS", "%s", full_line);
    } else if (level == WQMS_LOG_LEVEL_ENUM_WARN) {
        ESP_LOGW("WQMS", "%s", full_line);
    } else if (level == WQMS_LOG_LEVEL_ENUM_ERROR) {
        ESP_LOGE("WQMS", "%s", full_line);
    }
}

// ============================================================
// Public Function: Flush All Logs
// ============================================================
void log_flush_all(void) {
    for (int i = 0; i < WQMS_LOG_TYPE_MAX; i++) {
        if (log_files[i] != NULL) {
            fflush(log_files[i]);
        }
    }
}

// ============================================================
// Public Function: Close All Logs
// ============================================================
void log_close_all(void) {
    for (int i = 0; i < WQMS_LOG_TYPE_MAX; i++) {
        if (log_files[i] != NULL) {
            log_rotate_close((wqms_log_type_t)i);
            log_files[i] = NULL;
        }
    }
}
