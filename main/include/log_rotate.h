// log_rotate.h
#ifndef LOG_ROTATE_H
#define LOG_ROTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration
#define LOG_MAX_LINE_LENGTH 512

// Line-based log entry
typedef struct {
    char* data;
    size_t length;
    uint32_t offset;  // Starting offset in file
} log_entry_t;

// Initialize log system (thread-safe)
esp_err_t log_rotate_init(void);

// Write a complete log line (automatically adds \n)
esp_err_t log_rotate_write_line(const char* line);

// Read next complete log line from current position
// Returns: number of bytes read, 0 if no more lines
size_t log_rotate_read_line(char* buffer, size_t buffer_size, uint32_t* read_offset);

// Get total number of complete log lines
uint32_t log_rotate_get_line_count(void);

// Get total log size in bytes (data area only)
size_t log_rotate_get_size(void);

// Check if log is empty
bool log_rotate_is_empty(void);

// Clear all logs
esp_err_t log_rotate_clear(void);

// Flush to disk
void log_rotate_flush(void);

// Close log system
void log_rotate_close(void);

// Check if log system is ready
bool log_rotate_is_ready(void);

// Get current write position (for debugging)
uint32_t log_rotate_get_write_pos(void);

#ifdef __cplusplus
}
#endif

#endif // LOG_ROTATE_H