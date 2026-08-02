// log_rotate.c
// Line-based circular buffer - entries separated by \n
// Thread-safe with reader-writer locks

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "system_config.h"
#include "log_rotate.h"
#include "log_levels.h"

// --- Static Variables ---
static FILE* log_file = NULL;
static bool initialized = false;

// Synchronization primitives
static SemaphoreHandle_t write_mutex = NULL;
static SemaphoreHandle_t read_mutex = NULL;
static SemaphoreHandle_t read_count_mutex = NULL;
static int active_readers = 0;

static const char* TAG = "log_rotate";

// ============================================================
// Internal: File operations
// ============================================================

static long get_file_size(FILE* file) {
    if (file == NULL) return 0;
    
    long current_pos = ftell(file);
    if (current_pos < 0) return 0;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, current_pos, SEEK_SET);
    
    return size;
}

static bool read_header(FILE* file, uint32_t* write_pos, uint32_t* line_count) {
    if (file == NULL || write_pos == NULL || line_count == NULL) return false;
    
    fseek(file, 0, SEEK_SET);
    
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, file) != 1) {
        return false;
    }
    
    if (magic != 0x4C4F4700) {
        return false;
    }
    
    if (fread(write_pos, sizeof(*write_pos), 1, file) != 1) {
        return false;
    }
    
    if (fread(line_count, sizeof(*line_count), 1, file) != 1) {
        return false;
    }
    
    return true;
}

static bool write_header(FILE* file, uint32_t write_pos, uint32_t line_count) {
    if (file == NULL) return false;
    
    fseek(file, 0, SEEK_SET);
    
    uint32_t magic = 0x4C4F4700;
    if (fwrite(&magic, sizeof(magic), 1, file) != 1) {
        return false;
    }
    
    if (fwrite(&write_pos, sizeof(write_pos), 1, file) != 1) {
        return false;
    }
    
    if (fwrite(&line_count, sizeof(line_count), 1, file) != 1) {
        return false;
    }
    
    // Write reserved bytes (zeros)
    uint32_t reserved = 0;
    if (fwrite(&reserved, sizeof(reserved), 1, file) != 1) {
        return false;
    }
    
    fflush(file);
    return true;
}

// ============================================================
// Internal: Initialize new log file
// ============================================================

static bool initialize_log_file(FILE* file) {
    if (file == NULL) return false;
    
    // Write header with write position at beginning of data
    if (!write_header(file, LOG_METADATA_SIZE, 0)) {
        return false;
    }
    
    // Pre-allocate the file to max size
    fseek(file, LOG_FILE_MAX_SIZE - 1, SEEK_SET);
    fputc('\0', file);
    fflush(file);
    
    return true;
}

// ============================================================
// Internal: Find next complete line starting from a position
// ============================================================

static uint32_t find_next_line_start(FILE* file, uint32_t start_pos) {
    if (file == NULL) return LOG_METADATA_SIZE;
    
    if (start_pos < LOG_METADATA_SIZE || start_pos >= LOG_FILE_MAX_SIZE) {
        return LOG_METADATA_SIZE;
    }
    
    fseek(file, start_pos, SEEK_SET);
    char ch;
    uint32_t scan_pos = start_pos;
    
    while (scan_pos < LOG_FILE_MAX_SIZE) {
        if (fread(&ch, 1, 1, file) != 1) {
            break;
        }
        if (ch == '\n') {
            // Found end of line, next position is start of next line
            uint32_t next_pos = scan_pos + 1;
            if (next_pos >= LOG_FILE_MAX_SIZE) {
                return LOG_METADATA_SIZE;
            }
            return next_pos;
        }
        scan_pos++;
    }
    
    return LOG_METADATA_SIZE;
}

// ============================================================
// Internal: Count complete lines in data area
// ============================================================

static uint32_t count_lines_in_data(FILE* file, uint32_t start, uint32_t end) {
    if (file == NULL || start >= end) return 0;
    
    fseek(file, start, SEEK_SET);
    char ch;
    uint32_t count = 0;
    uint32_t pos = start;
    
    while (pos < end) {
        if (fread(&ch, 1, 1, file) != 1) break;
        if (ch == '\n') {
            count++;
        }
        pos++;
    }
    
    return count;
}

// ============================================================
// Internal: Find the oldest complete line to wrap to
// ============================================================

static uint32_t find_wrap_position(FILE* file, uint32_t write_pos, size_t needed_space) {
    if (file == NULL) return LOG_METADATA_SIZE;
    
    // Start from the beginning of data area
    uint32_t scan_pos = LOG_METADATA_SIZE;
    uint32_t line_start = LOG_METADATA_SIZE;
    size_t freed_space = 0;
    bool found_line = false;
    
    while (scan_pos < write_pos && freed_space < needed_space) {
        // Find next newline
        fseek(file, scan_pos, SEEK_SET);
        char ch;
        uint32_t line_end = scan_pos;
        
        while (line_end < write_pos) {
            if (fread(&ch, 1, 1, file) != 1) break;
            line_end++;
            if (ch == '\n') {
                // Found complete line
                size_t line_length = line_end - line_start;
                freed_space += line_length;
                line_start = line_end;
                found_line = true;
                break;
            }
        }
        
        if (!found_line) break;
        found_line = false;
        scan_pos = line_start;
    }
    
    if (freed_space >= needed_space) {
        // Return the position after the last removed line
        return line_start;
    }
    
    return LOG_METADATA_SIZE; // Fallback to beginning
}

// ============================================================
// Lock Management
// ============================================================

static void reader_lock_acquire(void) {
    // Acquire read count mutex
    xSemaphoreTake(read_count_mutex, portMAX_DELAY);
    active_readers++;
    if (active_readers == 1) {
        // First reader, block writers
        xSemaphoreTake(write_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(read_count_mutex);
}

static void reader_lock_release(void) {
    xSemaphoreTake(read_count_mutex, portMAX_DELAY);
    active_readers--;
    if (active_readers == 0) {
        // Last reader, allow writers
        xSemaphoreGive(write_mutex);
    }
    xSemaphoreGive(read_count_mutex);
}

static void writer_lock_acquire(void) {
    xSemaphoreTake(write_mutex, portMAX_DELAY);
}

static void writer_lock_release(void) {
    xSemaphoreGive(write_mutex);
}

// ============================================================
// Public: Initialize log system
// ============================================================

esp_err_t log_rotate_init(void) {
    if (initialized) return ESP_OK;
    
    // Create synchronization primitives
    write_mutex = xSemaphoreCreateMutex();
    read_mutex = xSemaphoreCreateMutex();
    read_count_mutex = xSemaphoreCreateMutex();
    
    if (write_mutex == NULL || read_mutex == NULL || read_count_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }
    
    // Ensure directory exists
    mkdir("/spiffs/logs", 0755);
    
    // Try to open existing file
    log_file = fopen(LOG_FILE_PATH, "r+b");
    
    if (log_file == NULL) {
        log_file = fopen(LOG_FILE_PATH, "w+b");
        if (log_file == NULL) {
            ESP_LOGE(TAG, "Failed to create log file: %s", strerror(errno));
            return ESP_ERR_NOT_FOUND;
        }
        
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        
        ESP_LOGI(TAG, "Created new log file");
        initialized = true;
        return ESP_OK;
    }
    
    // Validate header
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        ESP_LOGI(TAG, "Invalid header, re-initializing");
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Validate write position
    if (write_pos < LOG_METADATA_SIZE || write_pos >= LOG_FILE_MAX_SIZE) {
        ESP_LOGI(TAG, "Invalid write pos, resetting");
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Log system initialized (write_pos: %d, lines: %d)", write_pos, line_count);
    return ESP_OK;
}

// ============================================================
// Public: Write complete log line
// ============================================================

esp_err_t log_rotate_write_line(const char* line) {
    if (!initialized || log_file == NULL || line == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t line_len = strlen(line);
    if (line_len == 0) return ESP_OK;
    
    // Ensure line doesn't exceed max length
    if (line_len > LOG_MAX_LINE_LENGTH) {
        line_len = LOG_MAX_LINE_LENGTH;
    }
    
    // Add 1 for newline character
    size_t total_len = line_len + 1;
    
    writer_lock_acquire();
    
    // Read current header
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        writer_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if we need to wrap
    uint32_t data_end = write_pos + total_len;
    
    if (data_end >= LOG_FILE_MAX_SIZE) {
        // Need to wrap - find complete line boundary
        uint32_t new_pos = find_wrap_position(log_file, write_pos, total_len);
        
        // Update line count (lines before new_pos are overwritten)
        uint32_t lines_to_remove = count_lines_in_data(log_file, LOG_METADATA_SIZE, new_pos);
        if (line_count > lines_to_remove) {
            line_count -= lines_to_remove;
        } else {
            line_count = 0;
        }
        
        write_pos = new_pos;
        data_end = write_pos + total_len;
    }
    
    // Write the line with newline
    if (data_end >= LOG_FILE_MAX_SIZE) {
        // Still wraps (shouldn't happen with logic above, but handle anyway)
        size_t first_chunk = LOG_FILE_MAX_SIZE - write_pos;
        size_t second_chunk = total_len - first_chunk;
        
        fseek(log_file, write_pos, SEEK_SET);
        if (fwrite(line, 1, first_chunk, log_file) != first_chunk) {
            ESP_LOGE(TAG, "Failed to write first chunk");
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        fseek(log_file, LOG_METADATA_SIZE, SEEK_SET);
        if (fwrite(line + first_chunk, 1, second_chunk, log_file) != second_chunk) {
            ESP_LOGE(TAG, "Failed to write second chunk");
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        write_pos = LOG_METADATA_SIZE + second_chunk;
    } else {
        // Normal write
        fseek(log_file, write_pos, SEEK_SET);
        if (fwrite(line, 1, line_len, log_file) != line_len) {
            ESP_LOGE(TAG, "Failed to write line");
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        // Write newline
        fputc('\n', log_file);
        write_pos = data_end;
    }
    
    // Increment line count
    line_count++;
    
    // Update header
    if (!write_header(log_file, write_pos, line_count)) {
        ESP_LOGE(TAG, "Failed to update header");
        writer_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    
    writer_lock_release();
    return ESP_OK;
}

// ============================================================
// Public: Read next complete log line
// ============================================================

size_t log_rotate_read_line(char* buffer, size_t buffer_size, uint32_t* read_offset) {
    if (!initialized || log_file == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    
    reader_lock_acquire();
    
    // Read current state
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        reader_lock_release();
        return 0;
    }
    
    // Validate offset
    if (*read_offset < LOG_METADATA_SIZE || *read_offset >= LOG_FILE_MAX_SIZE) {
        *read_offset = LOG_METADATA_SIZE;
    }
    
    // Check if we've reached the end
    if (*read_offset >= write_pos) {
        reader_lock_release();
        return 0;
    }
    
    // Find the next newline
    fseek(log_file, *read_offset, SEEK_SET);
    char ch;
    uint32_t pos = *read_offset;
    size_t bytes_read = 0;
    
    while (pos < write_pos && bytes_read < buffer_size - 1) {
        if (fread(&ch, 1, 1, log_file) != 1) {
            break;
        }
        pos++;
        
        if (ch == '\n') {
            // End of line reached
            buffer[bytes_read] = '\0';
            *read_offset = pos; // Update offset to start of next line
            reader_lock_release();
            return bytes_read;
        }
        
        buffer[bytes_read] = ch;
        bytes_read++;
    }
    
    // If we reached the end of data without newline, return what we have
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        *read_offset = pos;
        reader_lock_release();
        return bytes_read;
    }
    
    reader_lock_release();
    return 0;
}

// ============================================================
// Public: Get total number of complete log lines
// ============================================================

uint32_t log_rotate_get_line_count(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return line_count;
}

// ============================================================
// Public: Get total log size
// ============================================================

size_t log_rotate_get_size(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return write_pos - LOG_METADATA_SIZE;
}

// ============================================================
// Public: Check if log is empty
// ============================================================

bool log_rotate_is_empty(void) {
    return log_rotate_get_line_count() == 0;
}

// ============================================================
// Public: Clear log
// ============================================================

esp_err_t log_rotate_clear(void) {
    if (!initialized || log_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    writer_lock_acquire();
    bool result = initialize_log_file(log_file);
    writer_lock_release();
    
    return result ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// ============================================================
// Public: Flush log
// ============================================================

void log_rotate_flush(void) {
    if (log_file != NULL) {
        fflush(log_file);
    }
}

// ============================================================
// Public: Close log
// ============================================================

void log_rotate_close(void) {
    writer_lock_acquire();
    if (log_file != NULL) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    initialized = false;
    writer_lock_release();
    
    // Clean up mutexes
    if (write_mutex != NULL) {
        vSemaphoreDelete(write_mutex);
        write_mutex = NULL;
    }
    if (read_mutex != NULL) {
        vSemaphoreDelete(read_mutex);
        read_mutex = NULL;
    }
    if (read_count_mutex != NULL) {
        vSemaphoreDelete(read_count_mutex);
        read_count_mutex = NULL;
    }
}

// ============================================================
// Public: Check if log system is ready
// ============================================================

bool log_rotate_is_ready(void) {
    return initialized && log_file != NULL;
}

// ============================================================
// Public: Get current write position (for debugging)
// ============================================================

uint32_t log_rotate_get_write_pos(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return write_pos;
}