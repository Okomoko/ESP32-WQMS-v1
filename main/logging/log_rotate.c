// log_rotate.c
// Line-based circular buffer - entries separated by \n
// Thread-safe with reader-writer locks

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "system_config.h"
#include "log_rotate.h"
#include "log_levels.h"

#define LOG_HEADER_VERSION 0x00000001

static FILE* log_file = NULL;
static bool initialized = false;

static SemaphoreHandle_t write_mutex = NULL;
static SemaphoreHandle_t read_mutex = NULL;
static SemaphoreHandle_t read_count_mutex = NULL;
static int active_readers = 0;

static long get_file_size(FILE* file) {
    if (file == NULL) return 0;
    
    struct stat st;
    if (fstat(fileno(file), &st) == 0) {
        return st.st_size;
    }
    
    long current_pos = ftell(file);
    if (current_pos < 0) return 0;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, current_pos, SEEK_SET);
    
    return size;
}

static bool read_header(FILE* file, uint32_t* write_pos, uint32_t* line_count) {
    if (file == NULL || write_pos == NULL || line_count == NULL) {
        return false;
    }
    
    flockfile(file);
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        funlockfile(file);
        return false;
    }
    
    uint32_t magic, version, reserved;
    size_t read_items = 0;
    read_items += fread(&magic, sizeof(magic), 1, file);
    read_items += fread(&version, sizeof(version), 1, file);
    read_items += fread(write_pos, sizeof(*write_pos), 1, file);
    read_items += fread(line_count, sizeof(*line_count), 1, file);
    read_items += fread(&reserved, sizeof(reserved), 1, file);
    
    if (read_items != 5) {
        funlockfile(file);
        return false;
    }
    
    if (magic != 0x4C4F4700) {
        funlockfile(file);
        return false;
    }
    
    if (version != LOG_HEADER_VERSION) {
        funlockfile(file);
        return false;
    }
    
    if (*write_pos < LOG_METADATA_SIZE || *write_pos >= LOG_FILE_MAX_SIZE) {
        funlockfile(file);
        return false;
    }
    
    funlockfile(file);
    return true;
}

static bool write_header(FILE* file, uint32_t write_pos, uint32_t line_count) {
    if (file == NULL) {
        return false;
    }
    
    flockfile(file);
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        funlockfile(file);
        return false;
    }
    
    uint32_t magic = 0x4C4F4700;
    uint32_t version = LOG_HEADER_VERSION;
    uint32_t reserved = 0;
    
    size_t written = 0;
    written += fwrite(&magic, sizeof(magic), 1, file);
    written += fwrite(&version, sizeof(version), 1, file);
    written += fwrite(&write_pos, sizeof(write_pos), 1, file);
    written += fwrite(&line_count, sizeof(line_count), 1, file);
    written += fwrite(&reserved, sizeof(reserved), 1, file);
    
    if (written != 5) {
        funlockfile(file);
        return false;
    }
    
    fflush(file);
    fsync(fileno(file));
    
    funlockfile(file);
    return true;
}

static bool initialize_log_file(FILE* file) {
    if (file == NULL) return false;
    
    if (!write_header(file, LOG_METADATA_SIZE, 0)) {
        return false;
    }
    
    fflush(file);
    return true;
}

static uint32_t find_next_line_start(FILE* file, uint32_t start_pos) {
    if (file == NULL) return LOG_METADATA_SIZE;
    
    if (start_pos < LOG_METADATA_SIZE || start_pos >= LOG_FILE_MAX_SIZE) {
        return LOG_METADATA_SIZE;
    }
    
    if (fseek(file, start_pos, SEEK_SET) != 0) {
        return LOG_METADATA_SIZE;
    }
    
    char ch;
    uint32_t scan_pos = start_pos;
    
    while (scan_pos < LOG_FILE_MAX_SIZE) {
        if (fread(&ch, 1, 1, file) != 1) {
            break;
        }
        if (ch == '\n') {
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

static uint32_t count_lines_in_data(FILE* file, uint32_t start, uint32_t end) {
    if (file == NULL || start >= end) return 0;
    
    if (fseek(file, start, SEEK_SET) != 0) return 0;
    
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

static uint32_t find_wrap_position(FILE* file, uint32_t write_pos, size_t needed_space) {
    if (file == NULL) return LOG_METADATA_SIZE;
    
    uint32_t scan_pos = LOG_METADATA_SIZE;
    uint32_t line_start = LOG_METADATA_SIZE;
    size_t freed_space = 0;
    bool found_line = false;
    
    while (scan_pos < write_pos && freed_space < needed_space) {
        if (fseek(file, scan_pos, SEEK_SET) != 0) break;
        
        char ch;
        uint32_t line_end = scan_pos;
        
        while (line_end < write_pos) {
            if (fread(&ch, 1, 1, file) != 1) break;
            line_end++;
            if (ch == '\n') {
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
        return line_start;
    }
    
    return LOG_METADATA_SIZE;
}

static void reader_lock_acquire(void) {
    xSemaphoreTake(read_count_mutex, portMAX_DELAY);
    active_readers++;
    if (active_readers == 1) {
        xSemaphoreTake(write_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(read_count_mutex);
}

static void reader_lock_release(void) {
    xSemaphoreTake(read_count_mutex, portMAX_DELAY);
    active_readers--;
    if (active_readers == 0) {
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

esp_err_t log_rotate_init(void) {
    if (initialized) return ESP_OK;
    
    write_mutex = xSemaphoreCreateMutex();
    read_mutex = xSemaphoreCreateMutex();
    read_count_mutex = xSemaphoreCreateMutex();
    
    if (write_mutex == NULL || read_mutex == NULL || read_count_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    log_file = fopen(LOG_FILE_PATH, "r+b");
    
    if (log_file == NULL) {
        log_file = fopen(LOG_FILE_PATH, "w+b");
        if (log_file == NULL) {
            return ESP_ERR_NOT_FOUND;
        }
        
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        
        initialized = true;
        return ESP_OK;
    }
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        fclose(log_file);
        log_file = fopen(LOG_FILE_PATH, "w+b");
        if (log_file == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
        initialized = true;
        return ESP_OK;
    }
    
    if (write_pos < LOG_METADATA_SIZE || write_pos >= LOG_FILE_MAX_SIZE) {
        if (!initialize_log_file(log_file)) {
            fclose(log_file);
            log_file = NULL;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    initialized = true;
    return ESP_OK;
}

esp_err_t log_rotate_write_line(const char* line) {
    if (!initialized || log_file == NULL || line == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t line_len = strlen(line);
    if (line_len == 0) return ESP_OK;
    
    if (line_len > LOG_MAX_LINE_LENGTH) {
        line_len = LOG_MAX_LINE_LENGTH;
    }
    
    size_t total_len = line_len + 1;
    
    writer_lock_acquire();
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        writer_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t data_end = write_pos + total_len;
    
    if (data_end >= LOG_FILE_MAX_SIZE) {
        uint32_t new_pos = find_wrap_position(log_file, write_pos, total_len);
        uint32_t lines_to_remove = count_lines_in_data(log_file, LOG_METADATA_SIZE, new_pos);
        
        if (line_count > lines_to_remove) {
            line_count -= lines_to_remove;
        } else {
            line_count = 0;
        }
        
        write_pos = new_pos;
        data_end = write_pos + total_len;
    }
    
    if (data_end >= LOG_FILE_MAX_SIZE) {
        size_t first_chunk = LOG_FILE_MAX_SIZE - write_pos;
        size_t second_chunk = total_len - first_chunk;
        
        if (fseek(log_file, write_pos, SEEK_SET) != 0) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        if (fwrite(line, 1, first_chunk, log_file) != first_chunk) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        if (fseek(log_file, LOG_METADATA_SIZE, SEEK_SET) != 0) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        if (fwrite(line + first_chunk, 1, second_chunk, log_file) != second_chunk) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        write_pos = LOG_METADATA_SIZE + second_chunk;
    } else {
        if (fseek(log_file, write_pos, SEEK_SET) != 0) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        if (fwrite(line, 1, line_len, log_file) != line_len) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        if (fputc('\n', log_file) == EOF) {
            writer_lock_release();
            return ESP_ERR_INVALID_RESPONSE;
        }
        write_pos = data_end;
    }
    
    line_count++;
    
    if (!write_header(log_file, write_pos, line_count)) {
        writer_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    
    writer_lock_release();
    return ESP_OK;
}

size_t log_rotate_read_line(char* buffer, size_t buffer_size, uint32_t* read_offset) {
    if (!initialized || log_file == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    
    reader_lock_acquire();
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        reader_lock_release();
        return 0;
    }
    
    if (*read_offset < LOG_METADATA_SIZE || *read_offset >= LOG_FILE_MAX_SIZE) {
        *read_offset = LOG_METADATA_SIZE;
    }
    
    if (*read_offset >= write_pos) {
        reader_lock_release();
        return 0;
    }
    
    if (fseek(log_file, *read_offset, SEEK_SET) != 0) {
        reader_lock_release();
        return 0;
    }
    
    char ch;
    uint32_t pos = *read_offset;
    size_t bytes_read = 0;
    
    while (pos < write_pos && bytes_read < buffer_size - 1) {
        if (fread(&ch, 1, 1, log_file) != 1) {
            break;
        }
        pos++;
        
        if (ch == '\n') {
            buffer[bytes_read] = '\0';
            *read_offset = pos;
            reader_lock_release();
            return bytes_read;
        }
        
        buffer[bytes_read] = ch;
        bytes_read++;
    }
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        *read_offset = pos;
        reader_lock_release();
        return bytes_read;
    }
    
    reader_lock_release();
    return 0;
}

uint32_t log_rotate_get_line_count(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return line_count;
}

size_t log_rotate_get_size(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return write_pos - LOG_METADATA_SIZE;
}

bool log_rotate_is_empty(void) {
    return log_rotate_get_line_count() == 0;
}

esp_err_t log_rotate_clear(void) {
    if (!initialized || log_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    writer_lock_acquire();
    
    fclose(log_file);
    log_file = fopen(LOG_FILE_PATH, "w+b");
    if (log_file == NULL) {
        writer_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    
    bool result = initialize_log_file(log_file);
    writer_lock_release();
    
    return result ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void log_rotate_flush(void) {
    if (log_file != NULL) {
        fflush(log_file);
        fsync(fileno(log_file));
    }
}

void log_rotate_close(void) {
    writer_lock_acquire();
    if (log_file != NULL) {
        fflush(log_file);
        fsync(fileno(log_file));
        fclose(log_file);
        log_file = NULL;
    }
    initialized = false;
    writer_lock_release();
    
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

bool log_rotate_is_ready(void) {
    return initialized && log_file != NULL;
}

uint32_t log_rotate_get_write_pos(void) {
    if (!initialized || log_file == NULL) return 0;
    
    uint32_t write_pos, line_count;
    if (!read_header(log_file, &write_pos, &line_count)) {
        return 0;
    }
    
    return write_pos;
}