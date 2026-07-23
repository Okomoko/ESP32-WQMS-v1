// logger.c
// Core logging system with circular buffer and dual-level logging
// Uses fwrite to stdout (proper buffering) + direct web console buffer

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_err.h"

#include "logger.h"
#include "log_levels.h"
#include "log_rotate.h"
#include "system_config.h"
#include "web_console.h"

// ============================================================
// Buffer Sizes
// ============================================================
#define LOG_TIMESTAMP_LEN   32
#define LOG_MSG_LEN         256
#define LOG_FULL_LINE_LEN   512
#define LOG_FILE_LINE_LEN   512

// ============================================================
// ANSI Color Codes
// ============================================================
#define ANSI_COLOR_RED     "\033[0;31m"
#define ANSI_COLOR_GREEN   "\033[0;32m"
#define ANSI_COLOR_YELLOW  "\033[0;33m"
#define ANSI_COLOR_CYAN    "\033[0;36m"
#define ANSI_COLOR_WHITE   "\033[0;37m"
#define ANSI_COLOR_RESET   "\033[0m"

// ============================================================
// Static Variables
// ============================================================
static const char *log_type_names[] = {
    [WQMS_LOG_TYPE_SYSTEM] = "SYS",
    [WQMS_LOG_TYPE_APPLICATION] = "APP",
    [WQMS_LOG_TYPE_AUTOMATION] = "ATM",
    [WQMS_LOG_TYPE_NOTIFICATION] = "NTF",
    [WQMS_LOG_TYPE_API] = "API",
    [WQMS_LOG_TYPE_INTEGRATION] = "INT",
    [WQMS_LOG_TYPE_SENSOR] = "SEN",
    [WQMS_LOG_TYPE_RELAY] = "REL"
};

static const char *log_level_names[] = {
    [WQMS_LOG_LEVEL_ENUM_ERROR] = "ERR",
    [WQMS_LOG_LEVEL_ENUM_WARN] = "WAR",
    [WQMS_LOG_LEVEL_ENUM_INFO] = "INF",
    [WQMS_LOG_LEVEL_ENUM_DEBUG] = "DEB",
    [WQMS_LOG_LEVEL_ENUM_VERBOSE] = "VER"
};

static bool log_initialized = false;
static bool log_init_attempted = false;

// ============================================================
// Helper: Get color for log level
// ============================================================
static const char* get_level_color(wqms_log_level_t level) {
    switch(level) {
        case WQMS_LOG_LEVEL_ENUM_ERROR:   return ANSI_COLOR_RED;
        case WQMS_LOG_LEVEL_ENUM_WARN:    return ANSI_COLOR_YELLOW;
        case WQMS_LOG_LEVEL_ENUM_INFO:    return ANSI_COLOR_GREEN;
        case WQMS_LOG_LEVEL_ENUM_DEBUG:   return ANSI_COLOR_CYAN;
        case WQMS_LOG_LEVEL_ENUM_VERBOSE: return ANSI_COLOR_WHITE;
        default: return "";
    }
}

// ============================================================
// Helper: Check if level should go to console
// ============================================================
static bool should_log_to_console(wqms_log_level_t level) {
    int level_value = 0;
    switch(level) {
        case WQMS_LOG_LEVEL_ENUM_ERROR:   level_value = WQMS_LOG_LEVEL_ERROR; break;
        case WQMS_LOG_LEVEL_ENUM_WARN:    level_value = WQMS_LOG_LEVEL_WARN; break;
        case WQMS_LOG_LEVEL_ENUM_INFO:    level_value = WQMS_LOG_LEVEL_INFO; break;
        case WQMS_LOG_LEVEL_ENUM_DEBUG:   level_value = WQMS_LOG_LEVEL_DEBUG; break;
        case WQMS_LOG_LEVEL_ENUM_VERBOSE: level_value = WQMS_LOG_LEVEL_VERBOSE; break;
        default: return false;
    }
    return level_value <= WQMS_LOG_LEVEL_CONSOLE;
}

// ============================================================
// Helper: Check if level should go to file
// ============================================================
static bool should_log_to_file(wqms_log_level_t level) {
    int level_value = 0;
    switch(level) {
        case WQMS_LOG_LEVEL_ENUM_ERROR:   level_value = WQMS_LOG_LEVEL_ERROR; break;
        case WQMS_LOG_LEVEL_ENUM_WARN:    level_value = WQMS_LOG_LEVEL_WARN; break;
        case WQMS_LOG_LEVEL_ENUM_INFO:    level_value = WQMS_LOG_LEVEL_INFO; break;
        case WQMS_LOG_LEVEL_ENUM_DEBUG:   level_value = WQMS_LOG_LEVEL_DEBUG; break;
        case WQMS_LOG_LEVEL_ENUM_VERBOSE: level_value = WQMS_LOG_LEVEL_VERBOSE; break;
        default: return false;
    }
    return level_value <= WQMS_LOG_LEVEL_FILE;
}

// ============================================================
// Public Function: Initialize Logging
// ============================================================
void log_init(void) {
    if (log_initialized) return;
    if (log_init_attempted) return;
    log_init_attempted = true;
    
    // Initialize the circular buffer log file
    esp_err_t err = log_rotate_init();
    if (err != ESP_OK) {
        printf("[ERROR] Failed to initialize log system: %d\n", err);
        fflush(stdout);
        return;
    }

    log_initialized = true;
    
    // Write initialization messages with colors
    printf(ANSI_COLOR_GREEN "[SYST] [INFO] Logging system initialized (circular buffer mode)\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_CYAN "[SYST] [DEBUG] Console: DEBUG+, File: INFO+ (no DEBUG in files)\n" ANSI_COLOR_RESET);
    fflush(stdout);
    
    // Also write to web console (without colors)
    web_console_write("[SYST] [INFO] Logging system initialized (circular buffer mode)\n");
    web_console_write("[SYST] [DEBUG] Console: DEBUG+, File: INFO+ (no DEBUG in files)\n");
}

// ============================================================
// Public Function: Write Log Entry
// ============================================================
void wqms_log_write(wqms_log_type_t type, wqms_log_level_t level, const char *format, ...) {
    // 1. Validate inputs
    if (type >= WQMS_LOG_TYPE_MAX) return;
    if (level < WQMS_LOG_LEVEL_ENUM_ERROR || level > WQMS_LOG_LEVEL_ENUM_VERBOSE) return;
    
    // 2. Format message with timestamp and level
    char timestamp[LOG_TIMESTAMP_LEN];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    char msg_buffer[LOG_MSG_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
    va_end(args);

    // 3. Build full log line
    char full_line[LOG_FULL_LINE_LEN];
    snprintf(full_line, sizeof(full_line),
             "[%s] [%s-%s] %s",
             timestamp,
             log_type_names[type],
             log_level_names[level],
             msg_buffer);

    // ============================================================
    // 4. CONSOLE OUTPUT - printf (USB) + web_console_write (Web)
    //    printf goes to USB console with colors
    //    web_console_write goes to web console buffer (no colors)
    // ============================================================
    if (should_log_to_console(level)) {
        const char* color = get_level_color(level);
        
        // Build output with colors for BOTH USB and Web console
        char output[LOG_FULL_LINE_LEN + 32];
        int len = snprintf(output, sizeof(output), "%s%s\n%s", color, full_line, ANSI_COLOR_RESET);
        
        if (len > 0 && len < sizeof(output)) {
            // Write to USB console
            printf("%s", output);
            fflush(stdout);
            
            // Write to Web Console buffer (WITH colors for browser to parse)
            web_console_write(output);
        }
    }

    // ============================================================
    // 5. FILE OUTPUT (SPIFFS) - INFO+ only (no DEBUG)
    // ============================================================
    if (log_initialized && should_log_to_file(level)) {
        char file_line[LOG_FILE_LINE_LEN];
        int len = snprintf(file_line, sizeof(file_line), "%s\n", full_line);
        
        if (len > 0 && len < sizeof(file_line)) {
            esp_err_t err = log_rotate_write(file_line);
            if (err != ESP_OK) {
                // If file write fails, log to console
                printf(ANSI_COLOR_RED "[SYST] [ERROR] Failed to write to log file: %d\n" ANSI_COLOR_RESET, err);
                fflush(stdout);
                char err_msg[64];
                snprintf(err_msg, sizeof(err_msg), "[SYST] [ERROR] File write failed: %d\n", err);
                web_console_write(err_msg);
            }
        }
    }
}

// ============================================================
// Public Function: Flush All Logs
// ============================================================
void log_flush_all(void) {
    log_rotate_flush();
    fflush(stdout);
}

// ============================================================
// Public Function: Close All Logs
// ============================================================
void log_close_all(void) {
    log_rotate_close();
    log_initialized = false;
}