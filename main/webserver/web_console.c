// web_console.c - Complete with buffer protection
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "driver/uart.h"

#include "web_console.h"
#include "http_server.h"
#include "system_config.h"

#define WEB_CONSOLE_BUFFER_MASK  (WEB_CONSOLE_BUFFER_SIZE - 1)
#define WEB_CONSOLE_MAX_WRITE    (256)    // Max bytes per write

static char log_buffer[WEB_CONSOLE_BUFFER_SIZE];
static volatile size_t log_head = 0;
static volatile size_t log_tail = 0;
static volatile size_t log_count = 0;
static SemaphoreHandle_t buffer_mutex = NULL;
static vprintf_like_t s_next_vprintf = NULL;

// ============================================================
// Debug helper - writes directly to UART (bypasses everything)
// ============================================================
static void debug_uart(const char* msg) {
    uart_write_bytes(UART_NUM_0, msg, strlen(msg));
}

// ============================================================
// THE vprintf HOOK - With buffer protection
// ============================================================
static int web_console_vprintf(const char *fmt, va_list args) {
    // STEP 1: ALWAYS call original vprintf FIRST (for USB console)
    int result = 0;
    if (s_next_vprintf != NULL) {
        result = s_next_vprintf(fmt, args);
    } else {
        result = vprintf(fmt, args);
    }
    
    // STEP 2: Try to capture to buffer (with buffer full protection)
    if (buffer_mutex != NULL) {
        char temp_buf[256];
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(temp_buf, sizeof(temp_buf), fmt, args_copy);
        va_end(args_copy);
        
        if (len > 0 && len < sizeof(temp_buf)) {
            // Limit write size to prevent overflow
            if (len > WEB_CONSOLE_MAX_WRITE) {
                len = WEB_CONSOLE_MAX_WRITE;
            }
            
            // Try to take mutex with short timeout
            if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                // Check if buffer has enough space
                size_t available = WEB_CONSOLE_BUFFER_SIZE - log_count;
                
                if (available >= len) {
                    // Enough space - write normally
                    for (int i = 0; i < len; i++) {
                        log_buffer[log_head] = temp_buf[i];
                        log_head = (log_head + 1) & WEB_CONSOLE_BUFFER_MASK;
                        log_count++;
                    }
                } else {
                    // Not enough space - overwrite oldest entries
                    size_t bytes_to_write = len;
                    size_t bytes_to_drop = len - available;
                    
                    // Drop oldest entries
                    for (size_t i = 0; i < bytes_to_drop; i++) {
                        log_tail = (log_tail + 1) & WEB_CONSOLE_BUFFER_MASK;
                        log_count--;
                    }
                    
                    // Now write the new data
                    for (int i = 0; i < len; i++) {
                        log_buffer[log_head] = temp_buf[i];
                        log_head = (log_head + 1) & WEB_CONSOLE_BUFFER_MASK;
                        log_count++;
                    }
                }
                xSemaphoreGive(buffer_mutex);
            }
        }
    }
    
    return result;
}

// ============================================================
// Write directly to web console buffer (thread-safe)
// ============================================================
void web_console_write_len(const char* data, size_t len) {
    if (buffer_mutex == NULL || data == NULL || len == 0) return;
    if (len > WEB_CONSOLE_MAX_WRITE) len = WEB_CONSOLE_MAX_WRITE;
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        size_t available = WEB_CONSOLE_BUFFER_SIZE - log_count;
        
        if (available >= len) {
            // Enough space
            for (size_t i = 0; i < len; i++) {
                log_buffer[log_head] = data[i];
                log_head = (log_head + 1) & WEB_CONSOLE_BUFFER_MASK;
                log_count++;
            }
        } else {
            // Not enough space - drop oldest and write
            size_t bytes_to_drop = len - available;
            for (size_t i = 0; i < bytes_to_drop; i++) {
                log_tail = (log_tail + 1) & WEB_CONSOLE_BUFFER_MASK;
                log_count--;
            }
            for (size_t i = 0; i < len; i++) {
                log_buffer[log_head] = data[i];
                log_head = (log_head + 1) & WEB_CONSOLE_BUFFER_MASK;
                log_count++;
            }
        }
        xSemaphoreGive(buffer_mutex);
    }
}

void web_console_write(const char* data) {
    if (data == NULL) return;
    web_console_write_len(data, strlen(data));
}

// ============================================================
// HTTP Handler - With buffer protection
// ============================================================
esp_err_t web_console_handler(httpd_req_t *req) {
    if (buffer_mutex == NULL) {
        httpd_resp_send(req, "", 0);
        return ESP_OK;
    }
    
    // Take mutex and get a consistent snapshot
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Buffer busy");
        return ESP_FAIL;
    }
    
    size_t total_bytes = log_count;
    size_t read_pos = log_tail;
    xSemaphoreGive(buffer_mutex);
    
    if (total_bytes == 0) {
        httpd_resp_send(req, "", 0);
        return ESP_OK;
    }
    
    // Set response headers
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // Stream data in chunks
    char response_buffer[1024];
    size_t bytes_read = 0;
    size_t bytes_to_read = total_bytes;
    
    // Limit read to prevent excessive memory usage
    if (bytes_to_read > WEB_CONSOLE_BUFFER_SIZE) {
        bytes_to_read = WEB_CONSOLE_BUFFER_SIZE;
    }
    
    while (bytes_read < bytes_to_read) {
        size_t chunk_size = (bytes_to_read - bytes_read);
        if (chunk_size > sizeof(response_buffer) - 1) {
            chunk_size = sizeof(response_buffer) - 1;
        }
        
        if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            break;
        }
        
        for (size_t i = 0; i < chunk_size; i++) {
            response_buffer[i] = log_buffer[read_pos];
            read_pos = (read_pos + 1) & WEB_CONSOLE_BUFFER_MASK;
        }
        
        xSemaphoreGive(buffer_mutex);
        
        response_buffer[chunk_size] = '\0';
        if (httpd_resp_send_chunk(req, response_buffer, chunk_size) != ESP_OK) {
            return ESP_FAIL;
        }
        bytes_read += chunk_size;
    }
    
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ============================================================
// Web Console Initialization
// ============================================================
esp_err_t web_console_init(void) {
    debug_uart("[WEB_CONSOLE] Starting init...\n");
    
    // 1. Create mutex
    buffer_mutex = xSemaphoreCreateMutex();
    if (buffer_mutex == NULL) {
        debug_uart("[WEB_CONSOLE] ERROR: Failed to create mutex\n");
        return ESP_ERR_NO_MEM;
    }
    debug_uart("[WEB_CONSOLE] Mutex created\n");
    
    // 2. Initialize buffer
    memset(log_buffer, 0, WEB_CONSOLE_BUFFER_SIZE);
    log_head = 0;
    log_tail = 0;
    log_count = 0;
    
    // 3. Install vprintf hook
    debug_uart("[WEB_CONSOLE] Installing vprintf hook...\n");
    s_next_vprintf = esp_log_set_vprintf(web_console_vprintf);

    char addr_buf[32];
    snprintf(addr_buf, sizeof(addr_buf), "[WEB_CONSOLE] Hook installed: %p\n", (void*)s_next_vprintf);
    debug_uart(addr_buf);

    char stat_buf[64];
    snprintf(stat_buf, sizeof(stat_buf), "[WEB_CONSOLE] Count: %d\n", (int)log_count);
    debug_uart(stat_buf);

    debug_uart("[WEB_CONSOLE] Init complete!\n");
    return ESP_OK;
}

// ============================================================
// Debug function
// ============================================================
void web_console_print_stats(void) {
    if (buffer_mutex == NULL) {
        printf("[WEB_CONSOLE] Not initialized\n");
        return;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        printf("[WEB_CONSOLE] Stats: head=%d, tail=%d, count=%d, free=%d\n", 
               (int)log_head, (int)log_tail, (int)log_count,
               (int)(WEB_CONSOLE_BUFFER_SIZE - log_count));
        fflush(stdout);
        xSemaphoreGive(buffer_mutex);
    }
}

// ============================================================
// Clear buffer
// ============================================================
void web_console_clear(void) {
    if (buffer_mutex == NULL) return;
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memset(log_buffer, 0, WEB_CONSOLE_BUFFER_SIZE);
        log_head = 0;
        log_tail = 0;
        log_count = 0;
        xSemaphoreGive(buffer_mutex);
        printf("[WEB_CONSOLE] Buffer cleared\n");
    }
}