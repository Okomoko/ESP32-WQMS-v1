// log_rotate.c
// Circular buffer log rotation - NO file rename/delete operations!

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_err.h"

#include "system_config.h"
#include "log_rotate.h"
#include "log_levels.h"

// --- Static Variables ---
static FILE* log_file = NULL;
static bool initialized = false;

// ============================================================
// Internal: Get file size
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

// ============================================================
// Internal: Read header (ONLY 16 bytes!)
// ============================================================
static bool read_header(FILE* file, uint32_t* write_pos) {
    if (file == NULL || write_pos == NULL) return false;
    
    fseek(file, 0, SEEK_SET);
    
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, file) != 1) {
        return false;
    }
    
    // Check magic number (0x4C4F4700 = "LOG\0")
    if (magic != 0x4C4F4700) {
        return false;
    }
    
    if (fread(write_pos, sizeof(*write_pos), 1, file) != 1) {
        return false;
    }
    
    return true;
}

// ============================================================
// Internal: Write header (ONLY 16 bytes!)
// ============================================================
static bool write_header(FILE* file, uint32_t write_pos) {
    if (file == NULL) return false;
    
    fseek(file, 0, SEEK_SET);
    
    uint32_t magic = 0x4C4F4700;
    if (fwrite(&magic, sizeof(magic), 1, file) != 1) {
        return false;
    }
    
    if (fwrite(&write_pos, sizeof(write_pos), 1, file) != 1) {
        return false;
    }
    
    // Write reserved bytes (zeros)
    uint32_t reserved = 0;
    for (int i = 0; i < 2; i++) {
        if (fwrite(&reserved, sizeof(reserved), 1, file) != 1) {
            return false;
        }
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
    if (!write_header(file, LOG_METADATA_SIZE)) {
        return false;
    }
    
    // Pre-allocate the file to max size (doesn't use heap!)
    fseek(file, LOG_FILE_MAX_SIZE - 1, SEEK_SET);
    fputc('\0', file);
    fflush(file);
    
    WQMS_LOG_I("Initialized log file (%d bytes)", LOG_FILE_MAX_SIZE);
    return true;
}

// ============================================================
// Public: Initialize log system
// ============================================================
esp_err_t log_rotate_init(void) {
    if (initialized) return ESP_OK;
    
    // Ensure directory exists
    mkdir("/spiffs/logs", 0755);
    
    // Try to open existing file for read/write
    log_file = fopen(LOG_FILE_PATH, "r+b");
    
    if (log_file == NULL) {
        // File doesn't exist, create it
        log_file = fopen(LOG_FILE_PATH, "w+b");
        if (log_file == NULL) {
            WQMS_LOG_E("Failed to create log file: %s", strerror(errno));
            return ESP_ERR_NOT_FOUND;
        }
        
        // Initialize the file
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        
        WQMS_LOG_I("Created new log file");
        initialized = true;
        return ESP_OK;
    }
    
    // Check if file has valid header
    uint32_t write_pos;
    if (!read_header(log_file, &write_pos)) {
        // Invalid header, re-initialize
        WQMS_LOG_I("Invalid header, re-initializing file");
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
	if (write_pos > LOG_FILE_MAX_SIZE) {
		if (unlink(LOG_FILE_PATH) == 0) {
			WQMS_LOG_I("Log file deleted");
		} else {
			WQMS_LOG_W("File deletion failed (may not exist): %s", strerror(errno));
		}
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
	}

    // Check file size
    long size = get_file_size(log_file);
    if (size != LOG_FILE_MAX_SIZE) {
        // Size mismatch, re-initialize
        WQMS_LOG_I("Size mismatch, re-initializing file");
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    initialized = true;
    WQMS_LOG_D("Opened existing log file (write_pos: %d)", write_pos);
    return ESP_OK;
}

// ============================================================
// Public: Write log entry (streaming, minimal heap)
// ============================================================
esp_err_t log_rotate_write(const char* data) {
    if (!initialized || log_file == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t data_len = strlen(data);
    if (data_len == 0) return ESP_OK;
    
    // Max data size (leave room for null terminator)
    size_t max_data_size = LOG_FILE_MAX_SIZE - LOG_METADATA_SIZE - 1;
    if (data_len > max_data_size) {
        data_len = max_data_size; // Truncate if too long
    }
    
    // Read current write position
    uint32_t write_pos;
    if (!read_header(log_file, &write_pos)) {
        WQMS_LOG_E("Failed to read header for write, log file is corrupt.");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ensure write position is valid
    if (write_pos < LOG_METADATA_SIZE || write_pos > LOG_FILE_MAX_SIZE) {
        write_pos = LOG_METADATA_SIZE;
    }
    
    // Calculate where we'll write
    uint32_t data_end = write_pos + data_len + 1; // +1 for null terminator
    
    // Check if we need to wrap
    if (data_end >= LOG_FILE_MAX_SIZE) {
        // Need to wrap - write to end, then wrap to beginning
        size_t first_chunk = LOG_FILE_MAX_SIZE - write_pos - 1;
        size_t second_chunk = data_len - first_chunk;
        
        // Write first chunk (to end)
        fseek(log_file, write_pos, SEEK_SET);
        if (fwrite(data, 1, first_chunk, log_file) != first_chunk) {
            WQMS_LOG_E("Failed to write first chunk");
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        // Write second chunk (from beginning)
        fseek(log_file, LOG_METADATA_SIZE, SEEK_SET);
        if (fwrite(data + first_chunk, 1, second_chunk, log_file) != second_chunk) {
            WQMS_LOG_E("Failed to write second chunk");
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        // Null terminator at end of second chunk
        fseek(log_file, LOG_METADATA_SIZE + second_chunk, SEEK_SET);
        fputc('\0', log_file);
        
        // New write position
        write_pos = LOG_METADATA_SIZE + second_chunk + 1;
    } else {
        // No wrap needed
        fseek(log_file, write_pos, SEEK_SET);
        if (fwrite(data, 1, data_len, log_file) != data_len) {
            WQMS_LOG_E("Failed to write data");
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        // Null terminator
        fputc('\0', log_file);
        
        // New write position
        write_pos = data_end;
    }
    
    // Update header with new write position
    if (!write_header(log_file, write_pos)) {
        WQMS_LOG_E("Failed to update header");
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

// ============================================================
// Public: Read logs (for web console / log viewer)
// ============================================================
size_t log_rotate_read(char* buffer, size_t buffer_size, size_t offset) {
    if (!initialized || log_file == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    
    // Read write position
    uint32_t write_pos;
    if (!read_header(log_file, &write_pos)) {
        return 0;
    }
    
    // Ensure write position is valid
    if (write_pos < LOG_METADATA_SIZE || write_pos >= LOG_FILE_MAX_SIZE) {
        write_pos = LOG_METADATA_SIZE;
    }
    
    size_t total_read = 0;
    size_t file_pos = LOG_METADATA_SIZE + offset;
    
    // Don't read beyond the write position
    if (file_pos >= write_pos) {
        return 0;
    }
    
    // Read in chunks
    char chunk[LOG_READ_CHUNK_SIZE];
    
    while (file_pos < write_pos && total_read < buffer_size - 1) {
        // Calculate how much to read in this chunk
        size_t remaining = write_pos - file_pos;
        size_t chunk_size = (remaining < LOG_READ_CHUNK_SIZE) ? remaining : LOG_READ_CHUNK_SIZE;
        
        fseek(log_file, file_pos, SEEK_SET);
        size_t bytes_read = fread(chunk, 1, chunk_size, log_file);
        if (bytes_read == 0) break;
        
        // Process the chunk - replace null terminators with newlines
        for (size_t i = 0; i < bytes_read && total_read < buffer_size - 1; i++) {
            if (chunk[i] == '\0') {
                buffer[total_read++] = '\n';
            } else {
                buffer[total_read++] = chunk[i];
            }
        }
        
        file_pos += bytes_read;
    }
    
    buffer[total_read] = '\0';
    return total_read;
}

// ============================================================
// Public: Get total log size
// ============================================================
size_t log_rotate_get_size(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos;
    if (!read_header(log_file, &write_pos)) {
        return 0;
    }
    
    // Ensure write position is valid
    if (write_pos < LOG_METADATA_SIZE || write_pos >= LOG_FILE_MAX_SIZE) {
        write_pos = LOG_METADATA_SIZE;
    }
    
    return write_pos - LOG_METADATA_SIZE;
}

// ============================================================
// Public: Check if log is empty
// ============================================================
bool log_rotate_is_empty(void) {
    if (!initialized || log_file == NULL) return true;
    
    uint32_t write_pos;
    if (!read_header(log_file, &write_pos)) {
        return true;
    }
    
    return (write_pos <= LOG_METADATA_SIZE);
}

// ============================================================
// Public: Clear log
// ============================================================
esp_err_t log_rotate_clear(void) {
    if (!initialized || log_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Re-initialize the file
    if (!initialize_log_file(log_file)) {
        return ESP_ERR_INVALID_STATE;
    }
    
    WQMS_LOG_I("Log cleared");
    return ESP_OK;
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
    if (log_file != NULL) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    initialized = false;
}

// ============================================================
// Public: Check if log system is ready
// ============================================================
bool log_rotate_is_ready(void) {
    return initialized && log_file != NULL;
}
